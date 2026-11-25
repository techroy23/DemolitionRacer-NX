#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "replay.h"

static ReplayNativeOt ot;
static const uint32_t first[] = {
    0x06200000, 0xe1000e20, 0xe20abcde, 0xe3002004,
    0xe400280e, 0xe50017fd, 0xe6000003
};
static uint32_t second[] = {
    0x08210000, 0xa0000000, 5u|(8u<<16), 3u|(1u<<16), 0x12340021, 0xbeef5678,
    0xa0000000, 0, 0
};
static uint32_t tail[] = {0x02ffffff, 0x680000ff, 8u|(6u<<16)};
#define WORDS(a) (sizeof(a)/sizeof((a)[0]))
static const ReplayOtRange ranges[] = {
    {0x10000, first, WORDS(first)}, {0x200000, second, WORDS(second)},
    {0x210000, tail, WORDS(tail)}
};

static void supported(void) {
    second[0]=0x05210000;
    assert(replay_prepare_native_ot(&ot,reset_state,ranges,WORDS(ranges),0x10000,3,NULL)==REPLAY_OT_OK);
    assert(ot.plan.count==2 && ot.word_count==13);
    const ReplayDrawState s=ot.plan.state;
    assert(s.tp==0xe20 && s.dither==1 && s.window==0xabcde && s.mask==3);
    assert(s.x0==4 && s.y0==8 && s.x1==15 && s.y1==11 && s.dx==-3 && s.dy==2);
    const Job *jobs=ot.plan.jobs;
    assert(jobs[0].w[OP]==UPLOAD && jobs[0].w[FLAGS]==0 && jobs[0].w[SRC]==9);
    assert(jobs[1].w[OP]==REPLAY_RECT && (jobs[1].w[FLAGS]&(SET_MASK|TEST_MASK))==(SET_MASK|TEST_MASK));
    assert(jobs[1].w[LX]==5 && jobs[1].w[LY]==8 && jobs[1].w[TP]==0xe20);
    assert(second[4]==0x12340021 && tail[0]==0x02ffffff);
    second[4]=0; tail[0]=0;
    assert(ot.words[9]==0x12340021 && ot.words[10]==0xbeef5678);
    second[4]=0x12340021; tail[0]=0x02ffffff;
}

static void rejected(ReplayOtStatus status, uint32_t link, size_t offset, unsigned limit) {
    ReplayOtError error;
    const ReplayDrawState initial={0,0,1024,512,3,-2,7,9,2,0};
    assert(replay_prepare_native_ot(&ot,initial,ranges,WORDS(ranges),0x10000,limit,&error)==status);
    assert(error.link==link && error.word==offset);
    assert(ot.plan.count==0 && ot.word_count==0);
    assert(ot.plan.state.dx==3 && ot.plan.state.dy==-2 && ot.plan.state.tp==7 &&
           ot.plan.state.window==9 && ot.plan.state.mask==2);
    assert(second[4]==0x12340021);
}

static void late_rejections(void) {
    tail[1]=0x400000ff;
    tail[0]=0x03ffffff; /* Tag span exceeds the backing allocation. */
    rejected(REPLAY_OT_TRUNCATED,0x210000,0,3);
    tail[0]=0x02ffffff; /* Unsupported line after the previous upload tag. */
    rejected(REPLAY_OT_UNSUPPORTED,0x210000,0,3);
    tail[1]=0x600000ff; /* Supported command, missing its own trailing word. */
    rejected(REPLAY_OT_TRUNCATED,0x210000,0,3);
    tail[1]=0x680000ff;
    second[0]=0x08210000; /* Truncated second upload; do not spill into next tag. */
    rejected(REPLAY_OT_TRUNCATED,0x200000,5,3);
    second[0]=0x05210000;
    tail[0]=0x02200000;
    rejected(REPLAY_OT_CYCLE,0x200000,0,3);
    tail[0]=0x02210000;
    rejected(REPLAY_OT_CYCLE,0x210000,0,3);
    tail[0]=0x02010001;
    rejected(REPLAY_OT_BAD_LINK,0x10001,0,4);
    tail[0]=0x02030000;
    rejected(REPLAY_OT_BAD_LINK,0x30000,0,4);
    tail[0]=0x02ffffff;
    rejected(REPLAY_OT_TAG_LIMIT,0x210000,0,2);
}

static void map_boundaries(void) {
    const uint32_t root[]={0x00fffffc,0x01ffffff};
    const uint32_t last[]={REPLAY_OT_END};
    const ReplayOtRange map[]={{0,root,2},{0xfffffc,last,1}};
    assert(replay_prepare_native_ot(&ot,reset_state,map,2,0,2,NULL)==REPLAY_OT_OK);
    assert(ot.plan.count==0 && ot.word_count==0);
    assert(replay_prepare_native_ot(&ot,reset_state,NULL,0,REPLAY_OT_END,1,NULL)==REPLAY_OT_OK);
    assert(replay_prepare_native_ot(&ot,reset_state,map,2,0x01000000,2,NULL)==REPLAY_OT_BAD_LINK);
    const ReplayOtRange overlap[]={{0,root,2},{4,last,1}};
    assert(replay_prepare_native_ot(&ot,reset_state,overlap,2,0,2,NULL)==REPLAY_OT_BAD_INPUT);
    const ReplayOtRange adjacent[]={{0,root,2},{8,last,1}};
    assert(replay_prepare_native_ot(&ot,reset_state,adjacent,2,4,1,NULL)==REPLAY_OT_TRUNCATED);
}

static void capacity_limits(void) {
    /* Pack 127 two-word sprites per tag. The last tag exceeds MAX_JOBS. */
    enum { SPRITES=127, STRIDE=255, TAGS=(MAX_JOBS/SPRITES)+1 };
    static uint32_t sprites[TAGS*STRIDE];
    const ReplayOtRange jobs[]={ {0,sprites,WORDS(sprites)} };
    ReplayOtError error;
    for(unsigned i=0;i<TAGS;++i) {
        uint32_t *p=sprites+i*STRIDE;
        p[0]=(2u*SPRITES<<24)|(i+1==TAGS?REPLAY_OT_END:(i+1)*STRIDE*4u);
        for(unsigned j=0;j<SPRITES;++j)p[1+2*j]=0x680000ff;
    }
    assert(replay_prepare_native_ot(&ot,reset_state,jobs,1,0,TAGS,&error)==REPLAY_OT_JOB_LIMIT);
    assert(error.link==(TAGS-1)*STRIDE*4u && error.word==2u*(MAX_JOBS%SPRITES));
    assert(!ot.plan.count && !ot.word_count);

    enum { NOPS=255, NOP_STRIDE=256, NOP_TAGS=(MAX_WORDS/NOPS)+1 };
    static uint32_t nops[NOP_TAGS*NOP_STRIDE];
    const ReplayOtRange words[]={ {0,nops,WORDS(nops)} };
    for(unsigned i=0;i<NOP_TAGS;++i)
        nops[i*NOP_STRIDE]=(NOPS<<24u)|(i+1==NOP_TAGS?REPLAY_OT_END:(i+1)*NOP_STRIDE*4u);
    assert(replay_prepare_native_ot(&ot,reset_state,words,1,0,NOP_TAGS,&error)==REPLAY_OT_WORD_LIMIT);
    assert(error.link==(NOP_TAGS-1)*NOP_STRIDE*4u && !ot.plan.count && !ot.word_count);
}

int main(void) {
    supported();
    late_rejections();
    map_boundaries();
    capacity_limits();
    supported(); /* Reuse the exact plan/workspace after a rejection. */
    puts("whole native OT: cross-tag state, owned payload, late rejection and traversal limits passed");
    return 0;
}
