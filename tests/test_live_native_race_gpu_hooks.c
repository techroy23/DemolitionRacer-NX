/* Exercise actual generated DrawOTag/StoreImage/scanout hooks against ANGLE. */
#define main shader_only_fixture
#include "test_race_gpu_scanout.c"
#undef main
#define __SWITCH__ 1
#include "generated_psx_gpu.c"
/* The test uses longjmp only over C frames. Avoid flaky Win64 SEH unwind
 * through the LTO-optimized rejection fixture. */
#if defined(__MINGW32__)
#define __USE_MINGW_SETJMP_NON_SEH 1
#endif
#include <setjmp.h>

#ifndef TEST_REAL_SCENE_PRODUCER
uint8 DRAM[PSX_DRAM_SIZE];
#endif
#ifndef TEST_REAL_SCENE_PRODUCER
uint32 SCRATCHPAD[256];
#endif
static jmp_buf stopped;
static bool permitted;
static unsigned scanouts;
static unsigned stock_uploads;
static bool compare_scanout;
static uint32 expected_scanout[320*240];
static bool perspective_requested;
bool switch_live_race_perspective_permitted(void) { return perspective_requested; }

bool switch_live_race_gpu_permitted(void) { return permitted; }
void switch_live_race_gpu_log(const char *message) { fprintf(stderr,"%s\n",message); }
_Noreturn void switch_abort_game(const char *why) { fprintf(stderr,"expected stop: %s\n",why); longjmp(stopped,1); }
uint64_t docked_presenter_context_generation(void) { return 42; }
DockedPresenterResult switch_present_race_texture(const DockedPresenterBorrowedTexture *t,int32_t w,int32_t h,FramebufferSource c) {
    assert(t->texture_name && t->context_generation==42 && w==320 && h==240 && c.width==320 && c.height==240);
    if (compare_scanout) {
        GLuint fbo;
        GLint previous;
        static uint8 rgba[320*240*4];
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);
        glGenFramebuffers(1,&fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->texture_name,0);
        assert(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        assert(glGetError()==GL_NO_ERROR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,previous);
        glDeleteFramebuffers(1,&fbo);
        for (int i=0;i<w*h;++i) {
            uint32 got=rgba[4*i]|((uint32)rgba[4*i+1]<<8)|((uint32)rgba[4*i+2]<<16);
            if (got!=expected_scanout[i]) {
                fprintf(stderr,"scanout page y=%d pixel (%d,%d): stock=%06x GPU=%06x\n",
                        display_y,i%w,i/w,expected_scanout[i],got);
                abort();
            }
            assert(rgba[4*i+3]==255);
        }
    }
    ++scanouts;
    return DOCKED_PRESENTER_SUBMITTED;
}
sint32 xport_present(const uint32 *p,sint32 w,sint32 h,sint32 sx,sint32 sy,sint32 sw,sint32 sh,const char *s) {
    (void)p;(void)w;(void)h;(void)sx;(void)sy;(void)sw;(void)sh;(void)s;++stock_uploads;return 1;
}
sint32 xport_is_headless(void){return 0;}
uint64 xport_timer_get(void){return 0;}
sint32 xport_device_trace_enabled(void){return 0;}
sint32 xport_memory_pointer_identity(const void *p,size_t n,XportMemoryRegion *r,uint32 *o){(void)p;(void)n;(void)r;(void)o;return 0;}
sint32 xport_device_trace_begin(XportDeviceOperation *o,uint64 p,XportDevice d,XportDevicePrimitive a,XportDeviceDirection b,uint32 c,uint32 e){(void)o;(void)p;(void)d;(void)a;(void)b;(void)c;(void)e;abort();}
sint32 xport_device_trace_stage(XportDeviceOperation *o,XportDevicePhase a,XportDeviceEvidence b,XportDeviceRegion c,uint32 d,XportDeviceRegion e,uint32 f,uint32 g,uint32 h){(void)o;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;abort();}
sint32 xport_device_trace_complete(XportDeviceOperation *o,sint32 s){(void)o;(void)s;abort();}
sint32 xport_device_trace_gap(XportDeviceOperation *o,uint32 a){(void)o;(void)a;abort();}
#ifndef TEST_REAL_SCENE_PRODUCER
void *psx_addr(uint32 a,size_t n){a &= 0x1fffffffu; assert(a<sizeof(DRAM)&&n<=sizeof(DRAM)-a);return DRAM+a;}
#endif
sint32 xport_memory_readable(const void *a,size_t n){uintptr_t v=(uintptr_t)a,base=(uintptr_t)DRAM;return v>=base && v-base<sizeof(DRAM) && n<=sizeof(DRAM)-(v-base);}
#ifndef TEST_REAL_SCENE_PRODUCER
sint32 psx_callbacks_allowed(void){return 0;}
#endif
sint32 xport_store_u8(void *p,uint32 v){*(uint8*)p=(uint8)v;return 1;}
#ifndef TEST_REAL_SCENE_PRODUCER
void psx_sync_callbacks(void){}
#endif
#ifndef TEST_REAL_SCENE_PRODUCER
void psx_wait_signal_service(uint32 v){(void)v;}
#endif

#include "live_native_race_gpu_capture.inc"

static void verify_tiled_hazards(void) {
    /* RAW on the direct page, then wrapped WAR against a non-neighbor. */
    static const uint32 stream[]={
        0xe1000100u,
        0x680000f8u,8u<<16,
        0x65000000u,(8u<<16)|400u,8u<<8,0x10001u,
        0xe100010fu,
        0x65000000u,(8u<<16)|402u,(8u<<8)|64u,0x10001u,
        0x68f80000u,(8u<<16)|403u,
        0x6800f800u,8u<<16,
        /* Odd inline A0 updates its halfword without overwriting the WAR result. */
        0xa0000000u,(8u<<16)|403u,0x10001u,0x1234u,
        0x680000f8u,(8u<<16)|404u,
        0x6800f800u,(8u<<16)|405u,
    };
    uint32 *ot=psx_addr(0x1000,4+sizeof(stream));
    ot[0]=(sizeof(stream)/4u<<24)|0xffffffu;
    memcpy(ot+1,stream,sizeof(stream));
    gpu_set_display(0,8,320,240);
    permitted=true;
    switch_live_race_gpu_scope(true);
    DrawOTag(ot);
    assert(live_race_gpu && VRAM[8*1024]==0);
    PSX_RECT sampler={400,8,4,1};
    uint16_t *result=psx_addr(0x800c7488,512);
    assert(StoreImage(&sampler,(uint32 *)result)==0);
    assert(result[0]==0x001f && result[1]==0 && result[2]==0x001f && result[3]==0x1234);
    const RaceGpuMesaCounters stats=race_gpu_mesa_counters(live_race_gpu);
    assert(stats.jobs==8 && stats.dispatches==5 && stats.hazard_splits==2);
    assert(stats.tile_dispatches==2 && stats.serial_dispatches==3 && stats.peak_batch_jobs==3);
    assert(gpu_present()==1 && scanouts==1 && !stock_uploads);
    switch_live_race_gpu_scope(false);
    assert(VRAM[8*1024]==0x03e0 && VRAM[8*1024+404]==0x001f && VRAM[8*1024+405]==0x03e0);
    puts("Generated native hooks: texture RAW and wrapped non-neighbor WAR, packed halfwords and inline A0 passed");
}

static void textured_page(unsigned page, unsigned mode, bool raw)
{
    const unsigned palette_x=mode==0?896u:1008u, palette_y=510u;
    const unsigned index=mode==0?1u:20u;
    const unsigned palette_entry=(palette_x+index)&1023u;
    const unsigned tx=960u, ty=mode==0?256u:0u;
    const unsigned tap_x=mode==0?tx+2:tx+4;
    const unsigned clut=(palette_y<<6)|(palette_x/16u);
    const unsigned tpage=(tx/64u)|(ty?16u:0u)|(mode<<7);
    const unsigned window=1u|(1u<<5)|(1u<<10)|(1u<<15); /* (0,0) -> (8,8) */
    const uint32 color=(raw?0x35000000u:0x34000000u)|0x808080u;
    const uint32 packet[]={0x0bffffffu,0xe1000200u,0xe2000000u|window,
        color,16u|(4u<<16),clut<<16,
        color,24u|(4u<<16),tpage<<16,
        color,16u|(12u<<16),0};
    uint32 *ot=psx_addr(0x1000,sizeof(packet));
    uint32 *empty=psx_addr(0x2000,4);
    *empty=0x00ffffffu;
    memcpy(ot,packet,sizeof(packet));
    gpu_set_display(0,page,320,240);
    DRAWENV env={0};
    env.clip=(PSX_RECT){0,page,320,240};
    env.ofs[1]=page;
    PSX_RECT tex={tap_x,ty+8,1,1}, pal={palette_entry,palette_y,1,1};
    uint32 index_word=index, palette_word=mode==0?0x1234u:0x4210u;
    static uint16 initial[sizeof(VRAM)/sizeof(VRAM[0])];
    static uint16 expected[sizeof(VRAM)/sizeof(VRAM[0])];
    memcpy(initial,VRAM,sizeof(VRAM));

    PutDrawEnv(&env);
    LoadImagePSX(&tex,&index_word);
    LoadImagePSX(&pal,&palette_word);
    DrawOTag(ot);
    gpu_convert_display();
    memcpy(expected_scanout,present_pixels,sizeof(expected_scanout));
    memcpy(expected,VRAM,sizeof(VRAM));
    assert(expected[(page+4)*1024+16]!=initial[(page+4)*1024+16]);

    memcpy(VRAM,initial,sizeof(VRAM));
    PutDrawEnv(&env);
    switch_live_race_gpu_scope(true);
    DrawOTag(empty); /* Seed before the separate texture and CLUT uploads. */
    assert(live_race_gpu);
    LoadImagePSX(&tex,&index_word);
    LoadImagePSX(&pal,&palette_word);
    DrawOTag(ot);
    compare_scanout=true;
    assert(gpu_present());
    compare_scanout=false;
    const uint32_t *actual=race_gpu_mesa_stage_boundary(live_race_gpu);
    assert(actual && !memcmp(actual,expected,sizeof(expected)));
    switch_live_race_gpu_shutdown();
    switch_live_race_gpu_scope(false);
    memcpy(VRAM,initial,sizeof(VRAM));
}

#ifdef TEST_REAL_SCENE_PRODUCER
uint32 xport_memory_read(uint32 address,uint32 width) {
    uint32 value=0; memcpy(&value,psx_addr(address,width),width); return value;
}
sint32 xport_memory_write(uint32 address,uint32 width,uint32 value,uint32 byte_mask) {
    uint8 *bytes=psx_addr(address,width);
    for(unsigned byte=0;byte<width;++byte) if(byte_mask&(1u<<byte)) bytes[byte]=(uint8)(value>>(8u*byte));
    return 1;
}
void cd_dispatch_ready_event(void) {}
extern uint32 scene_8004FDDC(uint32);
extern uint32 scene_800456AC(uint32);
extern uint32 sub_80042754(void);
extern uint32 sub_80039A10(void);
extern void game_800B9374(uint32);
sint32 switch_extra_track_section(void) { return 0; }
void switch_record_track_extension(uint32 objects,uint32 bytes) { (void)objects; (void)bytes; abort(); }
uint32 track_extra_packets(uint32 index,uint32 bytes) { (void)index; (void)bytes; abort(); }
void draft_agent3_missing_dispatch_packet(uint32 target,uint32 object) {
    if(target==0x8004fddcu) scene_8004FDDC(object);
    else if(target==0x800456acu) scene_800456AC(object);
    else abort();
}
uint32 draft_agent2_missing_draw_otag_result(uint32 root) {
    assert((root&0x1fffffffu)==0x2400u);
    DrawOTag(psx_addr(root,4)); return 0;
}
sint32 draft_agent1_missing_diagnostic(const char *format,...) { (void)format; abort(); }

static void setup_test_track(bool near_path, unsigned bytes)
{
    const uint32 object=0x80010000u;
    const uint32 section=0x8000fc00u;
    w_u32(0x800c7d60u,1u); w_u32(0x800c7db0u,section); w_u8(0x800c7db8u,2u);
    w_u32(section,1u); w_u32(section+724u,object-section);
    w_u32(0x80065164u,4096); w_u32(0x80065168u,4096); w_u32(0x8006516cu,4096);
    w_u16(0x80081b84u,4096); w_u16(0x80081b8cu,4096); w_u16(0x80081b94u,4096);
    w_u16(object+8u,8u); w_u32(object+24u,32u);
    w_u32(0x8005cd78u,near_path?0x800456acu:0x8004fddcu);
    w_u16(object+4u,512u);
    xport_gte_write_control(26u,128u);
    xport_gte_write_control(29u,256u);
    xport_gte_write_control(30u,256u);
    w_u32(0x80065118u,0x80070000u); w_u32(0x80070070u,0x80002000u);
    w_u32(0x8006557cu,0x80001000u); w_u32(0x80065584u,bytes);
    w_u32(0x8006554cu,2048u); w_u32(0x8006514cu,2048u);
    w_u32(0x800654f0u,257u);
    assert(sub_80042754()==0x80001000u);
    w_u32(0x8006511cu,320u); w_u32(0x80065120u,240u);
    w_u32(0x8004fcacu,0x8004fe90u);
    w_u32(object+44u,64u); w_u32(object+48u,96u);
}

static void emit_test_wall(unsigned depth_mode)
{
    const uint32 object=0x80010000u, vertices=object+64u, stream=object+96u;
    const sint16 projected_inputs[3][3]={{32,16,128},{40,72,256},{96,24,512}};
    setup_test_track(depth_mode==6,32u);
    for (unsigned vertex=0;vertex<3;++vertex) {
        unsigned depth=depth_mode==1?128:projected_inputs[vertex][2];
        for (unsigned axis=0;axis<2;++axis)
            w_u16(vertices+vertex*8u+axis*2u,projected_inputs[vertex][axis]*depth/128u);
        w_u16(vertices+vertex*8u+4u,depth-32u);
    }
    w_u32(stream,depth_mode==6?0x01000001u:1u); w_u32(stream+4u,0u); w_u32(stream+8u,0x011a4000u);
    w_u16(stream+12u,0x0040u); w_u8(stream+15u,1u); w_u8(stream+16u,2u);
    w_u8(stream+22u,0x25u);
    // AVSZ3 computes floor((z0+z1+z2)/16); point its bucket at the link tail.
    const unsigned bucket=depth_mode==1?24u:56u;
    w_u32(0x80002000u+bucket*4u,0x00002000u);
    w_u32(0x80002400u,0x2000u+bucket*4u);
    game_800B9374(0u);
    assert(r_u32(0x80065570u)==0x80001020u);
    assert(r_u32(0x80002000u+bucket*4u)==0x1000u);
    assert(r_u32(0x80001008u)==(32u|(16u<<16)));
    assert(r_u32(0x80001010u)==(40u|(72u<<16)));
    assert(r_u32(0x80001018u)==(96u|(24u<<16)));
}

static const int quad_xy[4][2]={{32,16},{32,112},{80,8},{80,56}};
static const unsigned quad_distinct_z[4]={128,256,512,1024};

static unsigned emit_test_quad(bool near_path, bool gouraud, unsigned mode)
{
    const uint32 vertices=0x80010040u, stream=0x80010060u;

    const unsigned bytes=gouraud?52u:40u, stride=gouraud?12u:8u;
    const unsigned kind=gouraud?3u:1u;
    unsigned bucket=0;
    setup_test_track(near_path,mode==8?bytes-4u:bytes);
    if(mode==9) w_u32(0x8006511cu,70u); // Producer viewport, not the raster scissor.
    w_u32(0x8004fcacu+4u*kind,gouraud?0x80050348u:0x8004ffdcu);
    memset(psx_addr(stream,64),0,64);
    w_u32(stream,(near_path?0x01000000u:0u)|(kind<<16)|1u);
    for(unsigned i=0;i<4;++i) {
        const unsigned z=mode==7?quad_distinct_z[i]:(mode==1 || i<2)?128u:256u;
        const bool overflow=(mode==2 && i==0)||(mode==3 && i==3);
        const unsigned x=(mode==6 && !i)?56u:(unsigned)quad_xy[i][0];
        const unsigned y=(mode==6 && !i)?60u:(unsigned)quad_xy[i][1];
        w_u16(vertices+8u*i,(overflow?x/2+1:x*z/128u));
        w_u16(vertices+8u*i+2u,(overflow?y/2+1:y*z/128u));
        w_u16(vertices+8u*i+4u,(overflow?32u:z)-32u);
        w_u8(stream+16u+i,i);
        bucket+=overflow?32u:z;
    }
    bucket/=16u;
    w_u32(stream+4u,0u); w_u32(stream+8u,0x011a4000u);
    w_u16(stream+12u,0x0040u); w_u16(stream+14u,0x4040u);
    w_u32(0x800835d4u,0x808080u);
    if(!gouraud) w_u8(stream+26u,0x2cu);
    else w_u8(stream+29u,0x3cu);
    w_u32(0x80002000u+bucket*4u,0x2000u);
    w_u32(0x80002400u,0x2000u+bucket*4u);
    game_800B9374(0u);
    assert(r_u32(0x80065570u)==0x80001000u+bytes);
    assert(r_u32(0x80002000u+bucket*4u)==0x1000u);
    assert((r_u32(0x80001004u)>>24)==(gouraud?0x3cu:0x2cu));
    for(unsigned i=0;i<4;++i) {
        if((mode==2 && i==0)||(mode==3 && i==3)||(mode==6 && i==0)) continue;
        assert(r_u32(0x80001008u+i*stride)==(uint32)(quad_xy[i][0]|(quad_xy[i][1]<<16)));
    }
    return bytes;
}

static void verify_producer_quad(unsigned fixture)
{
    const unsigned mode=fixture%10u;
    const bool near_path=(fixture/10u)&1u, gouraud=(fixture/20u)&1u;
    static uint16 seed[1024*512], baseline[1024*512];
    static uint8 colors[320*240*4];
    gpu_set_display(0,8,320,240);
    DRAWENV env={0}; env.clip=(PSX_RECT){0,8,320,240}; env.ofs[1]=8;
    PutDrawEnv(&env);
    for(unsigned v=0;v<256;++v) for(unsigned u=0;u<256;++u)
        VRAM[(256u+v)*1024u+640u+u]=(uint16)((u&31u)|((v&31u)<<5)|0x4000u);
    memcpy(seed,VRAM,sizeof seed);
    uint32 *tail=psx_addr(0x2000,16);
    tail[0]=0x03ffffffu; tail[1]=0xe2000000u;
    tail[2]=0x680000f8u; tail[3]=200u|(2u<<16);
    emit_test_quad(near_path,gouraud,mode); sub_80039A10();
    memcpy(baseline,VRAM,sizeof baseline);
    gpu_convert_display(); memcpy(expected_scanout,present_pixels,sizeof expected_scanout);
    memcpy(VRAM,seed,sizeof seed); PutDrawEnv(&env);
    permitted=true; perspective_requested=true; switch_live_race_gpu_scope(true);
    const unsigned bytes=emit_test_quad(near_path,gouraud,mode);
    uint32 packet[13], *guest=psx_addr(0x1000,bytes);
    if(mode==4) guest[bytes/4u-1u]^=0x10000u; // Unused upper half of final UV.
    if(mode==5) { switch_race_perspective_begin_arena(0x80001000u,bytes); switch_race_perspective_seal(0x80002400u); }
    memcpy(packet,guest,bytes);
    sub_80039A10();
    assert(!memcmp(packet,guest,bytes));
    assert(live_race_gpu && race_gpu_mesa_counters(live_race_gpu).tile_dispatches==1);
    const bool enhanced=mode==0 || mode==6 || mode==7 || (near_path && mode==9);
    const RacePerspectiveCoverage *coverage=perspective_coverage+1;
    assert(coverage->enhanced==(mode==6?1u:enhanced?2u:0u));
    assert(coverage->equal==(mode==1?2u:0u));
    assert(coverage->producer[RACE_PERSPECTIVE_RTPT]==(mode==2));
    assert(coverage->producer[RACE_PERSPECTIVE_RTPS]==(mode==3));
    assert(coverage->changed==(mode==4));
    assert(coverage->missing==(mode==2 || mode==3 || mode==5 || mode==8 || (!near_path && mode==9)));
    assert(coverage->producer[RACE_PERSPECTIVE_CLIP]==(!near_path && mode==9));
    assert(perspective_bounds==(mode==8));
    if(enhanced) {
        unsigned texture=race_gpu_mesa_scanout(live_race_gpu,0,8,320,240,0,0,320,240,0,0);
        GLuint framebuffer; assert(texture);
        glGenFramebuffers(1,&framebuffer); glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,colors);
        assert(glGetError()==GL_NO_ERROR);
        unsigned compared[2]={0}, changed[2]={0};
        for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
            const uint8 *p=colors+4u*(y*320u+x);
            const unsigned stock=baseline[(8u+y)*1024u+x];
            assert(p[3]==255 && !!(p[0]||p[1]||p[2])==!!stock);
            if(x<=32 || x>=80 || !stock) continue;
            // Independent rational oracle for the projected planar wall. Skip
            // exact half-texel ties (float implementation) and stock edge rounding.
            const int dx=(int)x-32, dy=(int)y-16, denom=96-dx;

            const int diagonal=48*((int)y-112)+104*dx;
            if(48*dy+8*dx<=48 || 48*((int)y-112)+56*dx>=-48 || abs(diagonal)<=48) continue;
            const unsigned half=diagonal>0;
            if(mode==6 && !half) continue;
            int un=64*dx, ud=denom, vn=64*(6*dy+dx), vd=6*denom;
            if(mode==7) {
                // Exact integer area weights with four distinct FIFO depths.
                // Both halves use the same packet-indexed UV table.
                const int st[4][2]={{0,0},{0,64},{64,0},{64,64}};
                un=vn=ud=0;
                for(unsigned i=0;i<3;++i) {
                    const unsigned k=half+i;
                    const int *a=quad_xy[half+(i+1u)%3u], *b=quad_xy[half+(i+2u)%3u];
                    const int area=(a[0]-(int)x)*(b[1]-(int)y)-(a[1]-(int)y)*(b[0]-(int)x);
                    const int weight=(half?area:-area)*(int)(1024u/quad_distinct_z[k]);
                    ud+=weight; un+=weight*st[k][0]; vn+=weight*st[k][1];
                }
                vd=ud;
            }
            if(((2*un)%ud==0 && un%ud) || ((2*vn)%vd==0 && vn%vd)) continue;
            const unsigned u=(2*un+ud)/(2*ud), v=(2*vn+vd)/(2*vd);
            const unsigned want=(u&31u)|((v&31u)<<5)|0x4000u;
            const unsigned got=(p[2]>>3)|((p[1]>>3)<<5)|((p[0]>>3)<<10);
            if(got!=want) fprintf(stderr,"quad oracle (%u,%u) half=%u uv=(%u,%u) got=%04x want=%04x stock=%04x\n",x,y,half,u,v,got,want,stock);
            assert(got==want);
            ++compared[half]; changed[half]+=stock!=want;
        }
        assert((mode==6 || (compared[0]>100 && changed[0]>100)) && compared[1]>100 && changed[1]>100);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
        printf("Quad oracle interior samples=%u+%u changed=%u+%u\n",compared[0],compared[1],changed[0],changed[1]);
    } else { compare_scanout=true; assert(gpu_present()); compare_scanout=false; }
    uint32 *actual=psx_addr(0x800c7488u,512u);
    if(mode==0) {
        // Sample a known visibly corrected pixel as a later texture input.
        // The new sprite must see the stock texel, not the scanout override.
        uint32 *feedback=psx_addr(0x3000,24);
        feedback[0]=0x05ffffffu; feedback[1]=0xe1000101u;
        feedback[2]=0x65000000u; feedback[3]=15u|(12u<<16);
        feedback[4]=11u|(18u<<8); feedback[5]=0x00010001u;
        DrawOTag(feedback);
        baseline[20u*1024u+15u]=baseline[18u*1024u+75u];
        PSX_RECT feedback_rect={15,20,1,1};
        assert(StoreImage(&feedback_rect,actual)==0 && (*actual&65535u)==baseline[20u*1024u+15u]);
    }
    PSX_RECT rectangle={52,40,4,4};
    assert(StoreImage(&rectangle,actual)==0);
    for(unsigned y=0;y<4;++y) assert(!memcmp(actual+2*y,baseline+(40u+y)*1024u+52u,8));
    assert(race_gpu_mesa_counters(live_race_gpu).boundary_bytes==0);
    switch_live_race_gpu_scope(false);
    assert(!memcmp(VRAM,baseline,sizeof VRAM));
    printf("Producer quad: %s %s mode=%u, scanout oracle and exact STORE/handoff\n",
        near_path?"near":"far",gouraud?"GT4":"FT4",mode);
}
#include "live_native_race_near.inc"
#endif

static void verify_perspective_wall(unsigned depth_mode)
{
    const unsigned width=320, page=8, sample_x=66, sample_y=34;
    static uint16 stock_vram[1024*512];
    const uint32 wall[]={0x07002000u,0x25000000u,32u|(16u<<16),0,
        96u|(24u<<16),0x011a0040u,40u|(72u<<16),0x4000u};
    uint32 *packet=psx_addr(0x1000,sizeof wall);
    uint32 *tail=psx_addr(0x2000,16);
    memcpy(packet,wall,sizeof wall);
    tail[0]=0x03ffffffu; tail[1]=0xe2000000u;
    tail[2]=0x6800f800u; tail[3]=(2u<<16)|200u; // A second job forces ordered tiles.
    for (unsigned y=0;y<256;++y)
        for (unsigned x=0;x<256;++x)
            VRAM[(256+y)*1024+640+x]=(uint16)((x<24?31:31<<5)|((y<12?31:0)<<10));
    memcpy(stock_vram,VRAM,sizeof VRAM);
    gpu_set_display(0,page,320,240);
    DRAWENV environment={0};
    environment.clip=(PSX_RECT){0,page,320,240};
    environment.ofs[1]=page;
    PutDrawEnv(&environment);
    DrawOTag(packet);
    assert(VRAM[(page+sample_y)*1024+sample_x]==0x03e0);
    memcpy(stock_vram,VRAM,sizeof VRAM);
    gpu_convert_display();
    memcpy(expected_scanout,present_pixels,sizeof expected_scanout);
    for (unsigned y=0;y<240;++y) memset(VRAM+(page+y)*1024,0,width*2);
    perspective_requested=depth_mode!=3;
    permitted=true;
    switch_live_race_gpu_scope(true);
    switch_race_perspective_begin_arena(0x80001000u,32);
    switch_race_perspective_track_scope(true);
    const uint32 depths[3]={128u,depth_mode==1?128u:512u,depth_mode==1?128u:depth_mode==2?0u:256u};
#ifdef TEST_REAL_SCENE_PRODUCER
    if(depth_mode<=1u || depth_mode==6) { emit_test_wall(depth_mode); sub_80039A10(); }
    else
#endif
    {
    switch_race_perspective_record_source(0x80001000u,packet,depths,RACE_PERSPECTIVE_DIRECT,false);
    if(depth_mode==4) packet[1]^=1u; // Benign body edit (unused color for raw texture): reject old provenance.
    if(depth_mode==5) switch_race_perspective_begin_arena(0x80001000u,32);
    switch_race_perspective_seal(0x80001000u);
    DrawOTag(packet);
    }
    assert(live_race_gpu && race_gpu_mesa_counters(live_race_gpu).tile_dispatches==1);
    const uint32 *sample=psx_addr(0x800c7488,512);
    PSX_RECT rectangle={sample_x,page+sample_y,1,1};
    assert(StoreImage(&rectangle,(uint32 *)sample)==0 && (sample[0]&65535u)==0x03e0);
    assert(race_gpu_mesa_counters(live_race_gpu).boundary_bytes==0);
    /* At (66,34), screen barycentrics are (1/4,1/2,1/4).
       Perspective yields (16,16); affine yields (32,16). */
    if(depth_mode==0 || depth_mode==6) {
        unsigned name=race_gpu_mesa_scanout(live_race_gpu,0,page,320,240,0,0,320,240,0,0);
        static uint8 colors[320*240*4]; GLuint framebuffer;
        assert(name);
        glGenFramebuffers(1,&framebuffer); glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,name,0);
        glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,colors);
        const uint8 *spot=colors+4u*(sample_y*320u+sample_x);
        assert(glGetError()==GL_NO_ERROR && spot[0]==0 && spot[1]==0 && spot[2]==255);
        for(unsigned pixel=0;pixel<320u*240u;++pixel) {
            assert(colors[4*pixel+3]==255);
            assert((colors[4*pixel] || colors[4*pixel+1] || colors[4*pixel+2])==
                   (stock_vram[page*1024u+(pixel/320u)*1024u+pixel%320u]!=0));
        }
        // Feed the enhanced location back through the fixed-function texture page.
        // GP0 must read the stock green texel, not the red scanout override.
        uint32 *feedback=psx_addr(0x3000,24);
        feedback[0]=0x05ffffffu; feedback[1]=0xe1000101u;
        feedback[2]=0x65000000u; feedback[3]=15u|(12u<<16);
        feedback[4]=2u|((page+sample_y)<<8); feedback[5]=0x00010001u;
        DrawOTag(feedback);
        PSX_RECT feedback_pixel={15,page+12,1,1};
        assert(StoreImage(&feedback_pixel,(uint32 *)sample)==0 && (sample[0]&65535u)==0x03e0);
        stock_vram[(page+12u)*1024u+15u]=0x03e0u;
        // Even a same-valued subsequent write must replace the display override.
        uint32 same_affine_pixel=0x03e0u;
        assert(LoadImagePSX(&rectangle,&same_affine_pixel)==0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
        assert(race_gpu_mesa_scanout(live_race_gpu,0,page,320,240,0,0,320,240,0,0)==name);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
        glReadPixels(sample_x,sample_y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,colors);
        assert(glGetError()==GL_NO_ERROR && colors[0]==0 && colors[1]==255 && colors[2]==0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
    } else {
        compare_scanout=true; assert(gpu_present()); compare_scanout=false;
    }
    switch_live_race_gpu_scope(false);
    assert(!memcmp(VRAM,stock_vram,sizeof VRAM));
    printf("Perspective wall: mode=%u oracle, exact STORE and handoff passed\n",depth_mode);
}

static void verify_planar_diagonal(unsigned flipped)
{
    // Camera-space parallelogram projects to a trapezoid. Left z=128,
    // right z=256. Exact u is 64*(x-32)/(128-x) at all rows.
    static const uint32 screen[]={32u|(16u<<16),80u|(8u<<16),32u|(112u<<16),80u|(56u<<16)};
    static const uint32 uv[]={0,64,0x4000,0x4040};
    static const uint32 depths[]={128,256,128,256};
    static const uint8 corners[2][2][3]={{{0,1,2},{1,3,2}},{{0,1,3},{0,3,2}}};
    static uint16 baseline[1024*512];
    uint32 *head=psx_addr(0x1000,64);
    gpu_set_display(0,8,320,240);
    DRAWENV environment={0};
    environment.clip=(PSX_RECT){0,8,320,240}; environment.ofs[1]=8;
    PutDrawEnv(&environment);
    for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x)
        VRAM[(256+y)*1024+640+x]=x<24?31:31<<5;
    perspective_requested=true; permitted=true; switch_live_race_gpu_scope(true);
    switch_race_perspective_begin_arena(0x80001000u,64);
    switch_race_perspective_track_scope(true);
    for(unsigned triangle=0;triangle<2;++triangle) {
        uint32 *packet=head+triangle*8;
        packet[0]=triangle?0x07ffffffu:0x07001020u;
        packet[1]=0x25000000u;
        uint32 packet_depths[3];
        for(unsigned vertex=0;vertex<3;++vertex) {
            const unsigned corner=corners[flipped][triangle][vertex];
            packet[2+vertex*2]=screen[corner];
            packet[3+vertex*2]=uv[corner]|(vertex==1?0x011a0000u:0u);
            packet_depths[vertex]=depths[corner];
        }
        switch_race_perspective_record_source(0x80001000u+triangle*32u,packet,packet_depths,RACE_PERSPECTIVE_DIRECT,false);
    }
    switch_race_perspective_seal(0x80001000u);
    permitted=false; DrawOTag(head); permitted=true;
    memcpy(baseline,VRAM,sizeof VRAM);
    for(unsigned y=0;y<240;++y) memset(VRAM+(y+8)*1024,0,320*2);
    DrawOTag(head);
    assert(perspective_coverage[0].enhanced==2 && race_gpu_mesa_counters(live_race_gpu).tile_dispatches==1);
    const unsigned texture=race_gpu_mesa_scanout(live_race_gpu,0,8,320,240,0,0,320,240,0,0);
    GLuint framebuffer; static uint8 colors[320*240*4];
    glGenFramebuffers(1,&framebuffer); glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,colors);
    assert(glGetError()==GL_NO_ERROR);
    unsigned changed=0,diagonal_samples=0;
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
        const uint8 *pixel=colors+4u*(y*320u+x);
        const uint16 stock=baseline[(y+8)*1024u+x];
        assert((pixel[0] || pixel[1] || pixel[2])==(stock!=0));
        if(stock) {
            const unsigned rounded_u=(2u*64u*(x-32u)+(128u-x))/(2u*(128u-x));
            assert(pixel[0]==0 && pixel[1]==(rounded_u<24?0:255) && pixel[2]==(rounded_u<24?255:0));
            changed+=(pixel[2]!=0)!=(stock==31);
            diagonal_samples+=flipped ? 48u*(y-16u)==40u*(x-32u) :
                (y>=8 && 48u*(y-8u)==104u*(80u-x));
        }
    }
    assert(changed && diagonal_samples>1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
    switch_live_race_gpu_scope(false);
    assert(!memcmp(VRAM,baseline,sizeof VRAM));
    printf("Planar diagonal %u: %u changed colors, %u exact shared-edge samples, affine handoff\n",flipped,changed,diagonal_samples);
}

int main(int argc,char **argv){
    const EGLAttrib attrs[]={0x3203,strtol(getenv("DRACER_ANGLE_BACKEND"),NULL,0),EGL_NONE};
    const EGLint cfg_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};
    const EGLint ctx_attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
    const EGLint surf_attrs[]={EGL_WIDTH,320,EGL_HEIGHT,240,EGL_NONE};
    EGLDisplay d=eglGetPlatformDisplay(0x3202,EGL_DEFAULT_DISPLAY,attrs);
    EGLConfig cfg; EGLint count;
    assert(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API));
    assert(eglChooseConfig(d,cfg_attrs,&cfg,1,&count)&&count==1);
    EGLContext ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctx_attrs);
    EGLSurface surf=eglCreatePbufferSurface(d,cfg,surf_attrs);
    assert(eglMakeCurrent(d,surf,surf,ctx));
#ifdef TEST_REAL_SCENE_PRODUCER
    if(argc==3 && !strcmp(argv[1],"--quad")) { verify_producer_quad((unsigned)atoi(argv[2])); return 0; }
    if(argc==3 && !strcmp(argv[1],"--near")) { verify_near_fixture((unsigned)atoi(argv[2])); return 0; }
#endif
    if (argc==3 && !strcmp(argv[1],"--diagonal")) { verify_planar_diagonal((unsigned)atoi(argv[2])); return 0; }
    if (argc==3 && !strcmp(argv[1],"--perspective")) { verify_perspective_wall((unsigned)atoi(argv[2])); return 0; }
    if (argc==3 && !strcmp(argv[1],"--texture-parity")) {
        const bool raw=!strcmp(argv[2],"raw");
        permitted=true;
        for (unsigned mode=0;mode<2;++mode) {
            unsigned page=mode?264u:8u;
            /* Stock oracle first, then real native preflight, uploads and scanout. */
            textured_page(page,mode,raw);
        }
        puts("Textured GT3: exact stock scanout and VRAM on both race pages");
        return 0;
    }
    if (argc==2 && !strcmp(argv[1],"--hazards")) { verify_tiled_hazards(); return 0; }
    if (argc>=2) { perspective_requested=getenv("DRACER_REPLAY_PERSPECTIVE")!=NULL; verify_captured_race(argv[1],argc==3 ? (unsigned)atoi(argv[2]) : 0); return 0; }
    uint32 *ot=psx_addr(0x1000,16),*tail=psx_addr(0x2000,16);
    /* Native unmasked odd A0 sets one exact game word. */
    const uint32 packet[]={0x04ffffffu,0xa0000000u,8u<<16,0x10001u,0xffffu};
    memcpy(ot,packet,sizeof(packet));
    gpu_set_display(0,8,320,240);
    switch_live_race_gpu_scope(true);
    DrawOTag(ot);
    assert(!live_race_gpu && VRAM[8*1024]==0xffffu);
    VRAM[8*1024]=0;
    permitted=true;
    DrawOTag(ot);
    assert(live_race_gpu && VRAM[8*1024]==0 && race_gpu_mesa_counters(live_race_gpu).jobs==1);
    PSX_RECT rect={0,8,16,16};
    uint32 *result=psx_addr(0x800c7488,512);
    assert(StoreImage(&rect,result)==0 && result[0]==0xffffu);
    assert(gpu_present()==1 && scanouts==1 && !stock_uploads);
    if (!setjmp(stopped)) {
        ot[0]=0x04002000u; ot[4]=0x3333u;
        tail[0]=0x03ffffffu;tail[1]=0x40000000u;tail[2]=0;tail[3]=1;
        DrawOTag(ot);
        abort();
    }
    assert(race_gpu_mesa_counters(live_race_gpu).jobs==1);
    assert(VRAM[8*1024]==0);
    assert(StoreImage(&rect,result)==0 && result[0]==0xffffu);
    switch_live_race_gpu_scope(false);
    assert(!live_race_gpu && live_race_latched && VRAM[8*1024]==0xffffu);
    assert(gpu_present()==1 && scanouts==1 && stock_uploads==1);
    puts("Generated native hooks: default stock, exact STORE, GPU-only scanout, late rejection, exact stock handoff passed");
    return 0;
}
