/* Optional private capture input, opened in place. Only the harness observes the
 * final full-VRAM checkpoint; no production frame readback is added. */
static void verify_captured_race(const char *path,unsigned stop_frame)
{
    FILE *f=fopen(path,"rb");
    assert(f);
    uint32_t header[6], record[3], payload[VRAM_BYTES/4];
    assert(fread(header,4,6,f)==6 && !memcmp(header,"DRRCAP1",7) && header[2]==1 && header[3]==24);
    uint32_t head=0, link=0, expect_link=0;
    bool in_ot=false, done=false;
    unsigned frames=0, stores=0, ots=0;
    uint64_t last_hash=0;
    permitted=true;
    switch_live_race_gpu_scope(true);
    while ((!stop_frame || frames<stop_frame) && fread(record,4,3,f)==3) {
        const unsigned t=record[0], flag=record[1], size=record[2];
        assert(!done && size<=sizeof(payload) && !(size&3u) && fread(payload,1,size,f)==size);
        switch(t){
            case 1: assert(!live_race_gpu && size==VRAM_BYTES); memcpy(VRAM,payload,size); break;
            case 2: case 13: {
                assert(!in_ot && (size==44 || size==52));
                if(t==2 && frames) break; /* only use the initial seed state */
                assert(!payload[9] && !payload[10]);
                if(t==13) {
                    assert(!(payload[1] & ~0x600u) && !payload[11]);
                    DRAWENV env={0};
                    env.clip=(PSX_RECT){payload[3],payload[4],payload[5]-payload[3]+1,payload[6]-payload[4]+1};
                    env.ofs[0]=(int16_t)payload[7];env.ofs[1]=(int16_t)payload[8];
                    env.tpage=(uint16_t)payload[0];env.dtd=!!(payload[1]&512);env.dfe=!!(payload[1]&1024);
                    assert(!payload[2]);
                    PutDrawEnv(&env);
                } else {
                    GpuRasterState state={.draw_mode=payload[0], .draw_flags=payload[1], .texture_window=payload[2],
                        .area={payload[3],payload[4],payload[5],payload[6]},.offset={payload[7],payload[8]}};
                    assert(gpu_restore_raster(&state));
                }
                break;
            }
            case 3: break;
            case 4:
                assert(!in_ot && size==12 && live_race_gpu);
                assert(gpu_present() && !stock_uploads);
                last_hash=payload[1]|((uint64_t)payload[2]<<32);
                ++frames;
                break;
            case 5:
                assert(in_ot && !flag && size==8 && payload[0]==expect_link);
                link=payload[0];expect_link=payload[1]&0xffffffu;
                *(uint32_t*)psx_addr(link,4)=payload[1];break;
            case 6:
                assert(in_ot && !flag && size==4u+4u*(payload[0]>>24) && *(uint32_t*)psx_addr(link,4)==payload[0]);
                memcpy(psx_addr(link,size),payload,size);break;
            case 8: assert(flag && size==4 && (payload[0]>>24==5 || payload[0]>>24==8)); break;
            case 9: case 10: {
                const unsigned w=payload[2],h=payload[3];
                assert(w && h && (uint64_t)w*h*2+16==size);
                PSX_RECT r={payload[0],payload[1],w,h};
                if(t==10) assert(!flag && LoadImagePSX(&r,&payload[4])==0);
                else {
                    uint32_t *dst=psx_addr(0x800c7488u,512u);
                    assert(flag && w==16 && h==16 && StoreImage(&r,dst)==0 && !memcmp(dst,&payload[4],512));
                    ++stores;
                }
                break;
            }
            case 14:
                assert(size==28 && !payload[6]);
                gpu_set_display(payload[0],payload[1],payload[2],payload[3]);
                gpu_display_offset(payload[4],payload[5]);break;
            case 16: assert(!in_ot && frames==12 && stores==12 && ots==24);done=true;break;
            case 18:
                assert(!in_ot && !flag && size==4);
                head=expect_link=payload[0];in_ot=true;break;
            case 19:
                assert(in_ot && size==4 && payload[0]==1 && expect_link==0xffffffu);
                DrawOTag(psx_addr(head,4));assert(live_race_gpu);
                ++ots;in_ot=false;break;
            default: abort();
        }
    }
    assert((stop_frame ? frames==stop_frame : done && feof(f)) && fclose(f)==0 && scanouts==frames && !stock_uploads);
    const RaceGpuMesaCounters stats=race_gpu_mesa_counters(live_race_gpu);
    printf("Before handoff: seed_bytes=%llu jobs=%llu dispatches=%llu GPU_STORE=%llu boundary_bytes=%llu\n",
           (unsigned long long)stats.seed_bytes,(unsigned long long)stats.jobs,(unsigned long long)stats.dispatches,
           (unsigned long long)stats.store_reads,(unsigned long long)stats.boundary_bytes);
    /* The first two independent uploads and first STORE precede the first
     * eligible OT: the one initial seed includes their stock effects. */
    assert(stats.seed_bytes==VRAM_BYTES && !stats.boundary_bytes && stats.store_reads+1==stores);
    switch_live_race_gpu_scope(false);
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<sizeof(VRAM);++i) hash=(hash^((const uint8_t*)VRAM)[i])*UINT64_C(1099511628211);
    assert(hash==last_hash && !live_race_gpu);
    if (stop_frame) {
        printf("Capture END %u exact via prefix handoff: %016llx\n",frames,(unsigned long long)hash);
        return;
    }
    printf("Private capture through generated hooks: %u OTs, %u exact STOREs, %u borrowed frames, exact final VRAM %016llx\n",
           ots,stores,frames,(unsigned long long)hash);
    /* Require at least 80% fewer dispatches on exactly the same lowered jobs. */
    fflush(stdout);
    assert(stats.jobs==32398 && stats.dispatches * 5 <= stats.jobs);
    assert(stats.tile_dispatches && stats.tile_dispatches+stats.serial_dispatches==stats.dispatches);
    assert(stats.peak_batch_jobs==MAX_BATCH_JOBS && stats.capacity_splits);
}
