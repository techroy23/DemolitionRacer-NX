#include "replay.h"
#include <string.h>

const ReplayDrawState reset_state={0,0,1024,512,0,0,0,0,0,0};
typedef struct { int x,y; unsigned a[5]; } Vertex;
static int min(int a,int b){return a<b?a:b;}
static int max(int a,int b){return a>b?a:b;}
static int s11(uint32_t v){return (int)(v&1023u)-(int)(v&1024u);}
static unsigned px(uint32_t w){return w&1023u;}
static unsigned py(uint32_t w){return (w>>16)&511u;}
static unsigned rgb(uint32_t v){return ((v>>3)&31u)|((v>>6)&0x3e0u)|((v>>9)&0x7c00u);}
static unsigned opflags(unsigned op, ReplayDrawState s){
    return ((op&4)?TEX:0)|((op&1)?RAW:0)|((op&2)?SEMI:0)|
           ((s.mask&1)?SET_MASK:0)|((s.mask&2)?TEST_MASK:0);
}
static int bounds(Job *j, int x,int y,int r,int b,ReplayDrawState s){
    x=max(x,max(0,s.x0)); y=max(y,max(0,s.y0));
    r=min(r,min(1024,s.x1)); b=min(b,min(512,s.y1));
    if(x>=r||y>=b)return 0;
    j->w[LX]=(unsigned)x; j->w[LY]=(unsigned)y; j->w[HX]=(unsigned)r; j->w[HY]=(unsigned)b;
    return 1;
}
/* Match at packed-word granularity, including wrapping texture/CLUT pages. */
static int overlap_x(const Job *j,unsigned x,unsigned width){
    unsigned l=j->w[LX]&~1u, r=(j->w[HX]+1u)&~1u;
    return (l<x+width && x<r) || (x+width>1024u && l<x+width-1024u);
}
PackedWordRectangle destination_words(const Job *job){
    return (PackedWordRectangle){job->w[LX]/2u,job->w[LY],(job->w[HX]+1u)/2u,job->w[HY]};
}
static int wrapped_words_intersect(PackedWordRectangle destination,unsigned x,unsigned width,
                                   unsigned y,unsigned height){
    /* Texture page, CLUT origins and widths are all even in halfword units. */
    const unsigned left=x/2u,right=(x+width)/2u;
    return destination.top<y+height && y<destination.bottom &&
           ((destination.left<right && left<destination.right) ||
            (right>512u && destination.left<right-512u));
}
int texture_reads_intersect(const Job *j,PackedWordRectangle destination){
    if(!(j->w[FLAGS]&TEX))return 0;
    unsigned t=j->w[TP], c=j->w[CL], mode=(t>>7)&3u, ty=(t&16u)*16u, cy=(c>>6)&511u;
    return wrapped_words_intersect(destination,(t&15u)*64u,64u<<min(mode,2),ty,256u) ||
           (mode<2 && wrapped_words_intersect(destination,(c&63u)*16u,mode?256u:16u,cy,1u));
}
static int push(Plan *p,Job j){
    if(p->count==MAX_JOBS || texture_reads_intersect(&j,destination_words(&j))) return 0;
    p->jobs[p->count++]=j; return 1;
}
static void edge(uint32_t *e,Vertex a,Vertex b){
    int dx=b.x-a.x,dy=b.y-a.y;
    int64_t step=dy?((int64_t)dx*4294967296LL + (dx<0?1-dy:dx>0?dy-1:0))/dy:0;
    e[0]=(unsigned)a.x; e[1]=(unsigned)a.y; e[2]=(unsigned)step;
    e[3]=(unsigned)((uint64_t)step>>32); e[4]=(unsigned)b.y;
}
static int triangle(Plan *p,Vertex a,Vertex b,Vertex c,unsigned tp,unsigned cl,unsigned flags,ReplayDrawState s){
    Job j={{0}};
    int l=min(a.x,min(b.x,c.x)),r=max(a.x,max(b.x,c.x));
    int t=min(a.y,min(b.y,c.y)),f=max(a.y,max(b.y,c.y));
    int64_t det=(int64_t)(b.x-a.x)*(c.y-a.y)-(int64_t)(c.x-a.x)*(b.y-a.y);
    if(!det||r-l>1023||f-t>511||!bounds(&j,l,t,r,f,s))return 1;
    j.w[OP]=TRI;j.w[FLAGS]=flags;j.w[TP]=tp;j.w[CL]=cl;j.w[WIN]=s.window;
    Vertex anchor=b.x<=a.x?(c.x<=b.x?c:b):(c.x<a.x?c:a);
    for(unsigned i=0;i<5;i++){
        int ab=(int)b.a[i]-(int)a.a[i],ac=(int)c.a[i]-(int)a.a[i];
        uint32_t dx=(uint32_t)(((int64_t)ab*(c.y-a.y)-(int64_t)ac*(b.y-a.y))*4096/det)*4096u;
        uint32_t dy=(uint32_t)(((int64_t)(b.x-a.x)*ac-(int64_t)(c.x-a.x)*ab)*4096/det)*4096u;
        j.w[ATTR+i*3]=anchor.a[i]*0x1000000u+0x800000u-dx*(unsigned)anchor.x-dy*(unsigned)anchor.y;
        j.w[ATTR+i*3+1]=dx;j.w[ATTR+i*3+2]=dy;
    }
    if(a.y>b.y){Vertex v=a;a=b;b=v;}
    if(b.y>c.y){Vertex v=b;b=c;c=v;}
    if(a.y>b.y){Vertex v=a;a=b;b=v;}
    edge(j.w+EDGE0,a,c);edge(j.w+EDGE1,a,b);edge(j.w+EDGE2,b,c);
    return push(p,j);
}
static void attach_perspective_triangle(Job *job, const Vertex *v, const uint32_t *z, unsigned arity){
    for(unsigned i=0;i<3;++i) if(!z[i] || z[i]>=65535u) return;
    job->w[PERSPECTIVE_ARITY]=arity;
    if(z[0]==z[1] && z[0]==z[2]) return;
    for(unsigned i=0;i<3;++i){
        job->w[PERSPECTIVE_VERTEX+i*3]=(uint16_t)v[i].x|((uint32_t)(uint16_t)v[i].y<<16);
        job->w[PERSPECTIVE_VERTEX+i*3+1]=v[i].a[0]|(v[i].a[1]<<8);
        job->w[PERSPECTIVE_VERTEX+i*3+2]=z[i];
    }
}
static int polygon(Plan *p,const uint32_t *w,unsigned op,ReplayDrawState *s,const uint32_t *depths){
    unsigned textured=!!(op&4), shaded=!!(op&16), quad=!!(op&8);
    unsigned tp=s->tp,cl=0, color=w[0], flags=opflags(op,*s);
    if(s->dither && (shaded || (textured && !(op&1))))flags|=DITHER;
    Vertex v[4];
    unsigned cursor=1;
    for(unsigned i=0;i<3+quad;i++){
        if(i&&shaded)color=w[cursor++];
        uint32_t xy=w[cursor++], uv=textured?w[cursor++]:0;
        v[i]=(Vertex){s11(xy)+s->dx,s11(xy>>16)+s->dy,
                       {uv&255u,(uv>>8)&255u,color&255u,(color>>8)&255u,(color>>16)&255u}};
        if(textured&&i==0)cl=uv>>16;
        if(textured&&i==1){tp=uv>>16;s->tp=(s->tp&~0x9ffu)|(tp&0x9ffu);}
    }
    for(unsigned half=0;half<=quad;++half){
        const unsigned slot=p->count;
        if(!triangle(p,v[half],v[half+1],v[half+2],tp,cl,flags,*s)) return 0;
        // A degenerate or out-of-bounds half emits no job. Do not infer the
        // surviving half from the total count; attach at the actual push site.
        if(depths && p->count!=slot)
            attach_perspective_triangle(p->jobs+slot,v+half,depths+half,3+quad);
    }
    return 1;
}
static int rect(Plan *p,const uint32_t *w,unsigned op,ReplayDrawState s){
    int x=s11(w[1]+(unsigned)s.dx), y=s11((w[1]>>16)+(unsigned)s.dy);
    unsigned t=!!(op&4), size=(op>>3)&3u;
    int width=size?(size==1?1:size==2?8:16):(int)px(w[2+t]);
    int height=size?width:(int)py(w[2+t]);
    Job j={{0}};
    if(!bounds(&j,x,y,x+width,y+height,s))return 1;
    j.w[OP]=REPLAY_RECT;j.w[FLAGS]=opflags(op,s);j.w[TP]=s.tp;j.w[WIN]=s.window;
    j.w[X0]=(unsigned)x;j.w[Y0]=(unsigned)y;j.w[COLOR]=w[0]&0xffffffu;
    if(t){j.w[CL]=w[2]>>16;j.w[SRC]=w[2]&0xffffu;}
    return push(p,j);
}
static int transfer(Plan *p,const uint32_t *w,unsigned op,unsigned index,ReplayDrawState s){
    unsigned x=px(w[1]),y=py(w[1]),where=op==0x80?3:2;
    unsigned width=((w[where]-1)&1023u)+1,height=(((w[where]>>16)-1)&511u)+1;
    unsigned sx=x,sy=y;
    Job j={{0}};
    if(op==2){
        x&=~15u;width=(px(w[2])+15u)&~15u;height=py(w[2]);
        if(!width||!height)return 1;
        j.w[OP]=FILL;j.w[COLOR]=rgb(w[0]);
    }else if(op==0xa0){
        j.w[OP]=UPLOAD;j.w[FLAGS]=(s.mask&1?SET_MASK:0)|(s.mask&2?TEST_MASK:0);
        j.w[SRC]=index+3;j.w[X0]=x;j.w[Y0]=y;
    }else{
        x=px(w[2]);y=py(w[2]);
        if(x==sx && y==sy)return 1;
        if(sx+width>1024u || sy+height>512u)return 0;
        j.w[OP]=COPY;j.w[SRC]=sx;j.w[COLOR]=sy;
    }
    if(x+width>1024u||y+height>512u)return 0;
    j.w[LX]=x;j.w[LY]=y;j.w[HX]=x+width;j.w[HY]=y+height;j.w[WIDTH]=width;
    if(op==0x80&&y<sy+height&&sy<y+height&&overlap_x(&j,sx,width)){
        /* Pinned xport snapshots one source row at a time, reversing if y>sy. */
        j.w[OP]=COPY_ROW;
        for(unsigned i=0;i<height;i++){
            unsigned row=y>sy?height-1-i:i;
            j.w[LY]=y+row;j.w[HY]=y+row+1;j.w[COLOR]=sy+row;
            if(!push(p,j))return 0;
        }
        return 1;
    }
    return push(p,j);
}
/* Append one bounded command span using staged state, never cross a native tag. */
static ReplayOtStatus append_words(Plan *p,ReplayDrawState *state,const uint32_t *words,
                                   size_t n,size_t base,size_t *bad,int native,uint32_t *last_e1,
                                   const uint32_t *depths){
    ReplayDrawState s=*state;
    size_t i=0;
    ReplayOtStatus status=REPLAY_OT_UNSUPPORTED;
    for(;i<n;){
        const uint32_t *w=words+i;
        uint32_t v=*w;
        unsigned op=v>>24,count=1;
        if(op==0||op==1){}
        else if(op>=0xe1&&op<=0xe6){
            switch(op){
            case 0xe1:s.tp=v&0x3fffu;s.dither=!!(v&0x200u);if(last_e1)*last_e1=v;break;
            case 0xe2:s.window=v&0xfffffu;break;
            case 0xe3:s.x0=v&1023u;s.y0=(v>>10)&511u;break;
            case 0xe4:s.x1=(v&1023u)+1;s.y1=((v>>10)&511u)+1;break;
            case 0xe5:s.dx=s11(v);s.dy=s11(v>>11);break;
            case 0xe6:s.mask=v&3u;break;
            }
        }else if(op>=0x20&&op<=0x3f){
            unsigned k=op&8?4:3;
            count=1+k*(1+!!(op&4))+(op&16?k-1:0);
            if(count>n-i)goto truncated;
            if(!polygon(p,w,op,&s,depths))goto cannot_lower;
        }else if(op>=0x60&&op<=0x7f){
            count=2+!!(op&4)+!(op&0x18);
            if(count>n-i)goto truncated;
            if(!rect(p,w,op,s))goto cannot_lower;
        }else if(op==2||(op>=0x80&&op<=0x9f)||op==0xa0){
            /* Native linked fill has field exclusions not represented in DrawState. */
            if(native && op==2)goto fail;
            if(op>=0x80&&op<=0x9f)op=0x80;
            count=op==0x80?4:3;
            if(count>n-i)goto truncated;
            if(op==0xa0){
                unsigned width=((w[2]-1)&1023u)+1,height=(((w[2]>>16)-1)&511u)+1;
                count+=(width*height+1)/2;
            }
            ReplayDrawState transfer_state=s;
            /* Native packet A0 delegates to LoadImagePSX, unlike MMIO's mask-aware FIFO. */
            if(native && op==0xa0)transfer_state.mask=0;
            if(count>n-i)goto truncated;
            if(!transfer(p,w,op,(unsigned)(base+i),transfer_state))goto cannot_lower;
        }else goto fail;
        i+=count;
    }
    *state=s;
    return REPLAY_OT_OK;
cannot_lower:
    if(p->count==MAX_JOBS)status=REPLAY_OT_JOB_LIMIT;
    goto fail;
truncated:
    status=REPLAY_OT_TRUNCATED;
fail:
    if(bad)*bad=i;
    return status;
}

static int prepare_words(Plan *p,ReplayDrawState s,const uint32_t *words,size_t n,size_t *bad,int native){
    p->count=0;p->state=s;
    if(n<=MAX_WORDS && (words||!n)){
        if(append_words(p,&s,words,n,0,bad,native,NULL,NULL)==REPLAY_OT_OK){p->state=s;return 1;}
    }else if(bad)*bad=0;
    p->count=0;
    return 0;
}
int replay_prepare(Plan *p,ReplayDrawState initial,const uint32_t *words,size_t n,size_t *bad){
    return prepare_words(p,initial,words,n,bad,0);
}
int replay_prepare_native_packet(Plan *p,ReplayDrawState initial,
                                 const uint32_t *packet,size_t packet_words,size_t *bad){
    if(!packet || !packet_words || packet_words!=1u+(packet[0]>>24)){
        p->count=0;p->state=initial;if(bad)*bad=0;
        return 0;
    }
    return prepare_words(p,initial,packet+1,packet_words-1,bad,1);
}

static int valid_ot_ranges(const ReplayOtRange *ranges,size_t count){
    uint32_t end=0;
    if(count>REPLAY_OT_MAX_RANGES || (count&&!ranges))return 0;
    for(size_t i=0;i<count;++i){
        const ReplayOtRange *r=ranges+i;
        if(!r->words || !r->word_count || ((uintptr_t)r->words%_Alignof(uint32_t)) ||
           (r->base&3u) || r->base<end || r->base>REPLAY_OT_END ||
           r->word_count>(REPLAY_OT_END+1u-r->base)/4u)return 0;
        end=r->base+4u*(uint32_t)r->word_count;
    }
    return 1;
}

static const uint32_t *ot_span(const ReplayOtRange *ranges,size_t count,uint32_t link,size_t *available){
    size_t lo=0,hi=count;
    while(lo<hi){
        size_t mid=lo+(hi-lo)/2;
        const ReplayOtRange *r=ranges+mid;
        if(link<r->base)hi=mid;
        else{
            size_t offset=(link-r->base)/4u;
            if(offset>=r->word_count)lo=mid+1;
            else{*available=r->word_count-offset;return r->words+offset;}
        }
    }
    return NULL;
}


ReplayOtStatus replay_prepare_native_ot_with_depths(ReplayNativeOt *out,ReplayDrawState initial,
                                        const ReplayOtRange *ranges,size_t range_count,
                                        uint32_t head,unsigned tag_limit,ReplayOtError *error,
                                        ReplayPacketDepths packet_depths){
    ReplayOtStatus status=REPLAY_OT_BAD_INPUT;
    ReplayDrawState staged=initial;
    size_t used=0,bad=0;
    unsigned tags=0;
    uint32_t last_e1=0;
    if(out){
        out->plan.count=0;
        out->plan.state=initial;
        out->word_count=0;
        out->last_e1=0;
    }
    if(!out || !tag_limit || tag_limit>REPLAY_OT_MAX_TAGS ||
       initial.dx < -1024 || initial.dx > 1023 || initial.dy < -1024 || initial.dy > 1023 ||
       initial.x0<0 || initial.x0>=1024 || initial.y0<0 || initial.y0>=512 ||
       initial.x1<1 || initial.x1>1024 || initial.y1<1 || initial.y1>512 ||
       initial.tp>0x3fffu || initial.window>0xfffffu || initial.mask>3 || initial.dither>1 ||
       !valid_ot_ranges(ranges,range_count))goto fail;
    memset(out->visited,0,sizeof out->visited);
    while(head!=REPLAY_OT_END){
        status=REPLAY_OT_BAD_LINK;
        if(head>REPLAY_OT_END || (head&3u))goto fail;
        const unsigned bit=1u<<((head>>2)&7u);
        uint8_t *seen=out->visited+(head>>5);
        if(*seen&bit){status=REPLAY_OT_CYCLE;goto fail;}
        if(tags==tag_limit){status=REPLAY_OT_TAG_LIMIT;goto fail;}
        ++tags;
        *seen|=(uint8_t)bit;
        size_t available;
        const uint32_t *packet=ot_span(ranges,range_count,head,&available);
        if(!packet)goto fail;
        const uint32_t tag=packet[0];
        const size_t n=tag>>24;
        if(n>=available){status=REPLAY_OT_TRUNCATED;goto fail;}
        /* Pinned native walker skips only this exact terminal black-line sentinel. */
        const int sentinel=head==0x10018u && tag==0x03ffffffu &&
                           packet[1]==0x40000000u && !packet[2] && !packet[3];
        if(n && !sentinel){
            if(n>MAX_WORDS-used){status=REPLAY_OT_WORD_LIMIT;goto fail;}
            const uint32_t *depths=NULL;
            if(packet_depths && replay_perspective_arity(packet[1]>>24,n))
                depths=packet_depths(head,packet,n);
            memcpy(out->words+used,packet+1,n*sizeof *packet);
            status=append_words(&out->plan,&staged,out->words+used,n,used,&bad,1,&last_e1,depths);
            if(status!=REPLAY_OT_OK)goto fail;
            used+=n;
        }
        head=tag&REPLAY_OT_END;
    }
    out->plan.state=staged;
    out->word_count=used;
    out->last_e1=last_e1;
    if(error)*error=(ReplayOtError){REPLAY_OT_END,0};
    return REPLAY_OT_OK;
fail:
    if(out)out->plan.count=0;
    if(error)*error=(ReplayOtError){head,bad};
    return status;
}

ReplayOtStatus replay_prepare_native_ot(ReplayNativeOt *out,ReplayDrawState initial,
                                        const ReplayOtRange *ranges,size_t range_count,
                                        uint32_t head,unsigned tag_limit,ReplayOtError *error){
    return replay_prepare_native_ot_with_depths(out,initial,ranges,range_count,head,tag_limit,error,NULL);
}
