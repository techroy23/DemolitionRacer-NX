/* Included by the real-producer GPU fixture. Stock split mode 1 emits two
 * leaves, with an AB midpoint (Z=192) that must be reprojected. */
static void emit_near_fixture(unsigned kind, unsigned mode)
{
    const uint32 vertices=0x80010040u, stream=0x80010060u, ot=0x80002000u;
    const bool quad=kind&1u, gouraud=kind&2u;
    const unsigned tri_bytes=gouraud?40u:32u, quad_bytes=gouraud?52u:40u;
    const unsigned bytes=quad?quad_bytes:tri_bytes;
    const bool parent=mode==1 || mode==4;
    setup_test_track(true,2*bytes);
    memset(psx_addr(stream,64),0,64);
    w_u32(stream,0x01000001u|(kind<<16));
    for(unsigned i=0;i<3u+quad;++i) {
        int x=quad_xy[i][0]-(mode==2 || mode==4?42:0), y=quad_xy[i][1];
        int z=(int)quad_distinct_z[i];
        int vx=x*z/128, vy=y*z/128;
        if((mode==3 && !i) || (mode==5 && i==2) || (mode==6 && i==3)) {
            z=mode==3?60:32; vx=x/2+1; vy=y/2+1;
        }
        if(mode==7 && i==0) { vx=-1025; }
        w_u16(vertices+8u*i,vx); w_u16(vertices+8u*i+2u,vy);
        w_u16(vertices+8u*i+4u,z-32);
        w_u8(stream+(quad?16u:14u)+i,i);
    }
    w_u32(stream+4u,0u); w_u32(stream+8u,0x011a4000u);
    w_u16(stream+12u,0x0040u);
    if(quad) w_u16(stream+14u,0x4040u);
    w_u32(0x800835d4u,0x808080u);
    const unsigned threshold=quad?(gouraud?28u:25u):(gouraud?23u:21u);
    w_u8(stream+threshold,100u);
    if(!gouraud) w_u8(stream+threshold+1u,quad?0x2cu:0x24u);
    for(unsigned i=0;i<3u+quad;++i) w_u8(stream+(quad?20u:17u)+i,i==0 && !parent?1u:100u);
    w_u32(0x80065190u,450u);
    /* Mode table entry 0 never emits at top level. Deliberately rotate this
     * no-split face: the original direct packet must not follow that rotation. */
    w_u32((quad?0x8005d5b8u:0x8005d638u),parent?1u:0u);
    w_u32((quad?0x8005d5b4u:0x8005d634u)+8u,1u);
    for(unsigned i=1;i<=256;++i) w_u32(ot+4u*i,(ot+4u*(i-1u))&0xffffffu);
    game_800B9374(0u);
    assert(r_u32(0x80065570u)==0x80001000u+bytes+(parent?0:tri_bytes));
}

static int near_cross(const int *a, const int *b, int x, int y)
{
    return (a[0]-x)*(b[1]-y)-(a[1]-y)*(b[0]-x);
}

static void verify_near_fixture(unsigned fixture)
{
    const unsigned kind=fixture%4u, mode=fixture/4u;
    const bool quad=kind&1u, gouraud=kind&2u;
    const bool invalid=mode>=3 && mode!=4, parent=mode==1 || mode==4;
    const bool all_invalid=mode==6 || (mode==5 && !quad);
    const unsigned stride=gouraud?12u:8u, bytes=quad?(gouraud?52u:40u):(gouraud?40u:32u);
    static uint16 seed[1024*512], baseline[1024*512];
    static uint8 colors[320*240*4];
    gpu_set_display(0,8,320,240);
    DRAWENV env={0}; env.clip=(PSX_RECT){0,8,320,240}; env.ofs[1]=8;
    PutDrawEnv(&env);
    for(unsigned v=0;v<256;++v) for(unsigned u=0;u<256;++u)
        VRAM[(256u+v)*1024u+640u+u]=(uint16)((u&31u)|((v&31u)<<5)|0x4000u);
    memcpy(seed,VRAM,sizeof seed);
    w_u32(0x80002000u,0x00ffffffu);
    emit_near_fixture(kind,mode); sub_80039A10();
    memcpy(baseline,VRAM,sizeof baseline);
    gpu_convert_display(); memcpy(expected_scanout,present_pixels,sizeof expected_scanout);
    uint32 packets[26];
    const unsigned packet_bytes=r_u32(0x80065570u)-0x80001000u;
    memcpy(packets,psx_addr(0x80001000u,packet_bytes),packet_bytes);
    memcpy(VRAM,seed,sizeof seed); PutDrawEnv(&env);
    permitted=true; perspective_requested=true; switch_live_race_gpu_scope(true);
    emit_near_fixture(kind,mode);
    assert(!memcmp(packets,psx_addr(0x80001000u,packet_bytes),packet_bytes));
    sub_80039A10();
    assert(!memcmp(packets,psx_addr(0x80001000u,packet_bytes),packet_bytes));
    assert(live_race_gpu);
    const RacePerspectiveProducerCoverage *leaves=perspective_producer[RACE_PERSPECTIVE_NEAR_LEAF];
    const RacePerspectiveProducerCoverage *parents=perspective_producer[RACE_PERSPECTIVE_NEAR_PARENT];
    assert(parents[quad].producer[RACE_PERSPECTIVE_SUBDIVIDE]==1u);
    assert(parents[quad].recorded==parent && parents[quad].matched==parent);
    assert(parents[quad].partial==(mode==4));
    const unsigned tagged_tri=parent || all_invalid?0u:quad || invalid?1u:2u;
    assert(leaves[0].matched==tagged_tri && leaves[0].recorded==tagged_tri);
    assert(leaves[1].matched==(quad && !parent && !invalid));
    assert(leaves[0].partial==(mode==2?tagged_tri:mode==7?1u:0u));
    assert(leaves[1].partial==(mode==2 && quad));
    assert(leaves[0].producer[RACE_PERSPECTIVE_RTPT]==(quad?(mode==6):mode==5?2u:invalid?1u:0u));
    assert(leaves[1].producer[RACE_PERSPECTIVE_RTPT]==(quad && invalid && mode!=6));
    assert(leaves[1].producer[RACE_PERSPECTIVE_RTPS]==(mode==6));
    if(!invalid) {
        assert(perspective_coverage[quad].enhanced==(quad?2u:parent?1u:2u));
        if(quad && !parent) assert(perspective_coverage[0].enhanced==1u);
    }
    if(all_invalid) {
        assert(!perspective_coverage[0].enhanced && !perspective_coverage[1].enhanced);
        compare_scanout=true; assert(gpu_present()); compare_scanout=false;
    } else if(invalid) {
        assert(perspective_coverage[0].enhanced==1u && !perspective_coverage[1].enhanced);
    }
    unsigned texture=race_gpu_mesa_scanout(live_race_gpu,0,8,320,240,0,0,320,240,0,0);
    GLuint framebuffer; assert(texture);
    glGenFramebuffers(1,&framebuffer); glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,colors);
    assert(glGetError()==GL_NO_ERROR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
        const uint8 *p=colors+4u*(y*320u+x);
        assert(p[3]==255 && !!(p[0]||p[1]||p[2])==!!baseline[(8u+y)*1024u+x]);
    }
    unsigned samples[3]={0}, changed[3]={0}, affine_samples=0, slot=0, feedback_x=0, feedback_y=0;
    for(unsigned leaf=0;leaf<(parent?1u:2u);++leaf) {
        const bool leaf_quad=quad && !leaf;
        const bool affine=invalid && (!leaf || all_invalid);
        const uint32 *packet=packets+(leaf?bytes/4u:0u);
        const int midpoint=mode==3?158:192;
        const int expected_depth[4]={leaf? (quad?midpoint:512):128,leaf?(quad?256:midpoint):parent?256:midpoint,
            leaf?(quad?1024:256):512,1024};
        int xy[4][2], uv[4][2];
        for(unsigned i=0;i<3u+leaf_quad;++i) {
            uint32 screen=packet[(8u+i*stride)/4u], st=packet[(12u+i*stride)/4u];
            xy[i][0]=(sint16)screen; xy[i][1]=(sint16)(screen>>16);
            uv[i][0]=st&255u; uv[i][1]=(st>>8)&255u;
        }
        if(mode==2 || mode==4) assert(xy[0][0]<0 || xy[1][0]<0);
        for(unsigned half=0;half<=leaf_quad;++half,++slot) {
            const int *a=xy[half], *b=xy[half+1u], *c=xy[half+2u];
            const int sign=near_cross(a,b,c[0],c[1])>0?1:-1;
            for(int y=0;y<240;++y) for(int x=0;x<320;++x) {
                const unsigned stock=baseline[(8+y)*1024+x];
                if(!stock) continue;
                int area[3]={sign*near_cross(b,c,x,y),sign*near_cross(c,a,x,y),sign*near_cross(a,b,x,y)};
                if(area[0]<=100 || area[1]<=100 || area[2]<=100) continue;
                const uint8 *p=colors+4u*(y*320u+x);
                const unsigned got=(p[2]>>3)|((p[1]>>3)<<5)|((p[0]>>3)<<10);
                if(affine) {
                    if(mode!=7) { assert(got==stock); ++affine_samples; }
                    continue;
                }
                double d=0,u=0,v=0;
                for(unsigned i=0;i<3;++i) {
                    double w=(double)area[i]/expected_depth[half+i];
                    d+=w; u+=w*uv[half+i][0]; v+=w*uv[half+i][1];
                }
                u=u/d+0.5; v=v/d+0.5;
                if(u-(int)u<0.001 || u-(int)u>0.999 || v-(int)v<0.001 || v-(int)v>0.999) continue;
                unsigned want=((int)u&31u)|(((int)v&31u)<<5)|0x4000u;
                if(got!=want) fprintf(stderr,"near kind=%u mode=%u leaf=%u half=%u x=%d y=%d got=%04x want=%04x stock=%04x\n",kind,mode,leaf,half,x,y,got,want,stock);
                assert(got==want);
                ++samples[slot]; changed[slot]+=stock!=want;
                if(mode==0 && stock!=want && !feedback_y) { feedback_x=x; feedback_y=y+8; }
            }
            /* A divide overflow at A moves the AB midpoint very close to B
             * in screen space. The valid sibling still needs an exact oracle. */
            if(!affine && mode!=3) assert(samples[slot]>30 && changed[slot]>30);
            if(!affine && mode==3) assert(samples[slot]>0 && changed[slot]>0);
        }
    }
    uint32 *actual=psx_addr(0x800c7488u,512u);
    if(mode==0) {
        assert(feedback_y && feedback_y<256 && feedback_x<256);
        /* Draw a raw sprite that samples a proven corrected scanout pixel.
         * Texture feedback and subsequent STORE must see the affine VRAM. */
        uint32 *feedback=psx_addr(0x3000,24);
        feedback[0]=0x05ffffffu; feedback[1]=0xe1000100u;
        feedback[2]=0x65000000u; feedback[3]=300u|(4u<<16);
        feedback[4]=feedback_x|(feedback_y<<8); feedback[5]=0x00010001u;
        DrawOTag(feedback);
        baseline[12u*1024u+300u]=baseline[feedback_y*1024u+feedback_x];
        PSX_RECT pixel={300,12,1,1};
        assert(StoreImage(&pixel,actual)==0 && (*actual&65535u)==baseline[12u*1024u+300u]);
    }
    PSX_RECT rectangle={52,40,4,4};
    assert(StoreImage(&rectangle,actual)==0);
    for(unsigned y=0;y<4;++y) assert(!memcmp(actual+2*y,baseline+(40u+y)*1024u+52u,8));
    assert(race_gpu_mesa_counters(live_race_gpu).boundary_bytes==0);
    switch_live_race_gpu_scope(false);
    assert(!memcmp(VRAM,baseline,sizeof VRAM));
    printf("Near producer %s%u mode=%u projective interior=%u+%u+%u changed=%u+%u+%u affine interior=%u; exact stock coverage, STORE/handoff\n",
        gouraud?"GT":"FT",quad?4:3,mode,samples[0],samples[1],samples[2],changed[0],changed[1],changed[2],affine_samples);
}
