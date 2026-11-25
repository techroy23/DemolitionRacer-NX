#include "race_gpu_mesa.h"
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <windows.h>
#endif
#include "live_raster.inc"
#include "live_scanout.inc"

enum { VRAM_BUFFER, JOB_BUFFER, INPUT_BUFFER, STAGING_BUFFER, DISPLAY_OVERRIDE_BUFFER, BUFFER_COUNT };
struct Gpu {
    EGLDisplay display;
    EGLContext context;
    GLuint program, buffers[BUFFER_COUNT];
    GLuint store_program, store_framebuffer, store_color, store_vao;
    GLint batch_configuration, store_rectangle, replay_viewport[4];
    GLuint scan_program, scan_fbo, scan_texture;
    GLint display_rect, crop_origin, display_offset;
    int scan_width, scan_height;
    bool seeded, failed, drained, perspective;
    RaceGpuMesaCounters counts;
    TileSchedule tiles; /* Reusable, zero-initialized mask scratch; not on the game stack. */
    uint32_t *boundary;
};

#define report(...) fprintf(stderr,__VA_ARGS__)
static bool check_gl(const char *where) {
    GLenum e=glGetError();
    if (e==GL_NO_ERROR) return true;
    report("Race GLES: %s: 0x%04x\n",where,(unsigned)e);
    return false;
}
static uint64_t now_ns(void) {
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick());
#else
    LARGE_INTEGER frequency,ticks;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&ticks);
    return (uint64_t)(ticks.QuadPart/frequency.QuadPart)*1000000000u +
        (uint64_t)(ticks.QuadPart%frequency.QuadPart)*1000000000u/frequency.QuadPart;
#endif
}

/* This include provides the same integer FBO and shader as the OLED replay.
 * Its standalone draw/restore function is not used in a borrowed context. */
#define RACE_GPU_SHARED_CONTEXT
#include "store_fbo_readback.inc"
#define gpu_replay submit_ordered_replay_jobs
#include "submit_replay_jobs.inc"
#undef gpu_replay

static const GLenum capabilities[]={GL_SCISSOR_TEST,GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,
    GL_CULL_FACE,GL_DITHER,GL_RASTERIZER_DISCARD,GL_SAMPLE_ALPHA_TO_COVERAGE,
    GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK};
static const GLenum pack_fields[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS};
#define COUNT(a) (sizeof(a)/sizeof(*(a)))
typedef struct { GLint name; GLint64 offset, size; } StorageBinding;
typedef struct {
    StorageBinding indexed[4];
    unsigned indexed_count;
    GLint storage, program, vao, read_fbo, draw_fbo, renderbuffer, texture, pack, copy_read, copy_write;
    GLint viewport[4], pack_state[COUNT(pack_fields)];
    GLboolean mask[4], enabled[COUNT(capabilities)];
} SavedGraphicsState;

static bool capture_graphics_state(SavedGraphicsState *s, bool perspective) {
    if (!check_gl("preexisting context error")) return false;
    GLboolean feedback=0;
    glGetBooleanv(GL_TRANSFORM_FEEDBACK_ACTIVE,&feedback);
    if(feedback) return false;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&s->storage);
    s->indexed_count=perspective?4u:3u;
    for(unsigned i=0;i<s->indexed_count;++i) {
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,i,&s->indexed[i].name);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,i,&s->indexed[i].offset);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,i,&s->indexed[i].size);
    }
    glGetIntegerv(GL_CURRENT_PROGRAM,&s->program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&s->vao);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&s->read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&s->draw_fbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&s->renderbuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&s->texture); /* never change active unit */
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&s->pack);
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING,&s->copy_read);
    glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING,&s->copy_write);
    glGetIntegerv(GL_VIEWPORT,s->viewport);
    glGetBooleanv(GL_COLOR_WRITEMASK,s->mask);
    for(unsigned i=0;i<COUNT(pack_fields);++i) glGetIntegerv(pack_fields[i],&s->pack_state[i]);
    for(unsigned i=0;i<COUNT(capabilities);++i) s->enabled[i]=glIsEnabled(capabilities[i]);
    return check_gl("capture context state");
}
static bool restore_graphics_state(const SavedGraphicsState *s) {
    for(unsigned i=0;i<s->indexed_count;++i) {
        StorageBinding b=s->indexed[i];
        if(b.name && b.size>0) glBindBufferRange(GL_SHADER_STORAGE_BUFFER,i,b.name,b.offset,b.size);
        else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i,b.name);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,s->storage);
    glUseProgram(s->program);
    glBindVertexArray(s->vao);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s->read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s->draw_fbo);
    glBindRenderbuffer(GL_RENDERBUFFER,s->renderbuffer);
    glBindTexture(GL_TEXTURE_2D,s->texture);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,s->pack);
    glBindBuffer(GL_COPY_READ_BUFFER,s->copy_read);
    glBindBuffer(GL_COPY_WRITE_BUFFER,s->copy_write);
    glViewport(s->viewport[0],s->viewport[1],s->viewport[2],s->viewport[3]);
    glColorMask(s->mask[0],s->mask[1],s->mask[2],s->mask[3]);
    for(unsigned i=0;i<COUNT(pack_fields);++i) glPixelStorei(pack_fields[i],s->pack_state[i]);
    for(unsigned i=0;i<COUNT(capabilities);++i) {
        if(s->enabled[i]) glEnable(capabilities[i]); else glDisable(capabilities[i]);
    }
    return check_gl("restore context state");
}
static void prepare_exact_draw(Gpu *gpu, GLuint framebuffer, int w, int h) {
    for(unsigned i=0;i<COUNT(capabilities);++i) glDisable(capabilities[i]);
    glColorMask(1,1,1,1);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);
    glViewport(0,0,w,h);
    glBindVertexArray(gpu->store_vao);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,gpu->buffers[VRAM_BUFFER]);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
static bool context_current(const Gpu *gpu) {
    return gpu && eglGetCurrentDisplay()==gpu->display && eglGetCurrentContext()==gpu->context;
}
static bool begin_operation(Gpu *gpu,SavedGraphicsState *s) {
    if (!gpu || gpu->failed || gpu->drained) return false;
    if (context_current(gpu) && capture_graphics_state(s,gpu->perspective)) return true;
    gpu->failed=true;
    return false;
}
static bool end_operation(Gpu *gpu,const SavedGraphicsState *s,bool ok) {
    bool restored=restore_graphics_state(s);
    if (!ok || !restored) gpu->failed=true;
    return ok && restored;
}
static GLuint link_program(GLenum stage,const char *source,const char *fragment) {
    GLuint first=compile_store_shader(stage,source);
    GLuint second=fragment?compile_store_shader(GL_FRAGMENT_SHADER,fragment):0;
    GLuint program=0;
    if(first && (!fragment || second)) {
        program=glCreateProgram();
        glAttachShader(program,first);
        if(second) glAttachShader(program,second);
        glLinkProgram(program);
        GLint linked=0;
        glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked) {
            char log[1024]={0}; glGetProgramInfoLog(program,sizeof log,NULL,log);
            report("Race GLES link: %s\n",log); glDeleteProgram(program); program=0;
        }
    }
    if(first) glDeleteShader(first);
    if(second) glDeleteShader(second);
    return program;
}
void race_gpu_mesa_close(Gpu *gpu) {
    if (!gpu) return;
    if (context_current(gpu)) {
        close_store_fbo(gpu);
        glDeleteFramebuffers(1,&gpu->scan_fbo);
        glDeleteTextures(1,&gpu->scan_texture);
        glDeleteBuffers(BUFFER_COUNT,gpu->buffers);
        if(gpu->scan_program) glDeleteProgram(gpu->scan_program);
        if(gpu->program) glDeleteProgram(gpu->program);
    }
    free(gpu->boundary);
    free(gpu);
}
Gpu *race_gpu_mesa_open_current_with_perspective(bool perspective) {
    GLint major=0,minor=0,alignment=0,bindings=0,groups=0;
    if (eglGetCurrentContext()==EGL_NO_CONTEXT) return NULL;
    glGetIntegerv(GL_MAJOR_VERSION,&major); glGetIntegerv(GL_MINOR_VERSION,&minor);
    if (major<3 || (major==3 && minor<1)) return NULL;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&alignment);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,&bindings);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,0,&groups);
    if (alignment<=0 || sizeof(Job)%(unsigned)alignment || bindings<(perspective?4:3) || groups<(GLint)TILE_COUNT ||
        !check_gl("compute capabilities")) return NULL;
    Gpu *gpu=calloc(1,sizeof(*gpu));
    if(!gpu) return NULL;
    gpu->perspective=perspective;
    gpu->context=eglGetCurrentContext(); gpu->display=eglGetCurrentDisplay();
    SavedGraphicsState saved;
    if (!begin_operation(gpu,&saved)) { free(gpu); return NULL; }
    gpu->program=link_program(GL_COMPUTE_SHADER,perspective?shader_source_perspective:shader_source,NULL);
    gpu->scan_program=link_program(GL_VERTEX_SHADER,store_vertex_source,perspective?scanout_source_perspective:scanout_source);
    bool ok=gpu->program && gpu->scan_program && open_store_fbo(gpu);
    if(ok) {
        glGenBuffers(perspective?BUFFER_COUNT:DISPLAY_OVERRIDE_BUFFER,gpu->buffers);
        if(perspective) {
            void *zeros=calloc(1,VRAM_BYTES*2u);
            if(!zeros) { end_operation(gpu,&saved,false); race_gpu_mesa_close(gpu); return NULL; }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,gpu->buffers[DISPLAY_OVERRIDE_BUFFER]);
            glBufferData(GL_SHADER_STORAGE_BUFFER,VRAM_BYTES*2u,zeros,GL_DYNAMIC_COPY);
            free(zeros);
        }
        glGenFramebuffers(1,&gpu->scan_fbo);
        glGenTextures(1,&gpu->scan_texture);
        gpu->batch_configuration=glGetUniformLocation(gpu->program,"batchConfiguration");
        gpu->display_rect=glGetUniformLocation(gpu->scan_program,"displayRect");
        gpu->crop_origin=glGetUniformLocation(gpu->scan_program,"cropOrigin");
        gpu->display_offset=glGetUniformLocation(gpu->scan_program,"displayOffset");
        ok=check_gl("create race resources") && gpu->batch_configuration>=0 && gpu->display_rect>=0 &&
            gpu->crop_origin>=0 && gpu->display_offset>=0;
    }
    if(!end_operation(gpu,&saved,ok)) { race_gpu_mesa_close(gpu); return NULL; }
    return gpu;
}
Gpu *race_gpu_mesa_open_current(void) { return race_gpu_mesa_open_current_with_perspective(false); }
bool race_gpu_mesa_seed(Gpu *gpu,const void *bytes) {
    SavedGraphicsState saved;
    if(!gpu || !bytes || gpu->seeded || !begin_operation(gpu,&saved)) return false;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,gpu->buffers[VRAM_BUFFER]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,VRAM_BYTES,bytes,GL_DYNAMIC_COPY);
    if(!end_operation(gpu,&saved,check_gl("VRAM seed"))) return false;
    gpu->seeded=true; gpu->counts.seed_bytes=VRAM_BYTES;
    return true;
}
static void bind_raster(Gpu *gpu) {
    glUseProgram(gpu->program);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,gpu->buffers[VRAM_BUFFER]);
    if(gpu->perspective) glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,gpu->buffers[DISPLAY_OVERRIDE_BUFFER]);
}
static bool emit_raster(Gpu *gpu,const Job *jobs,unsigned count,const uint32_t *words,
                        size_t word_count,const TileSchedule *schedule) {
    if(!submit_ordered_replay_jobs(gpu,jobs,count,words,word_count,schedule)) return false;
    gpu->counts.jobs+=count;
    if(schedule) {
        ++gpu->counts.tile_dispatches;
        ++gpu->counts.dispatches;
        gpu->counts.tile_workgroups+=schedule->count;
        if(schedule->count>gpu->counts.peak_tiles) gpu->counts.peak_tiles=schedule->count;
    } else {
        gpu->counts.serial_dispatches+=count;
        gpu->counts.dispatches+=count;
    }
    return true;
}
bool race_gpu_mesa_submit(Gpu *gpu,const Job *jobs,unsigned count,const uint32_t *words,
                         size_t word_count,const TileSchedule *schedule) {
    SavedGraphicsState saved;
    if(!gpu || !gpu->seeded || (!jobs && count) || !begin_operation(gpu,&saved)) return false;
    uint64_t start=now_ns();
    bind_raster(gpu);
    bool ok=emit_raster(gpu,jobs,count,words,word_count,schedule);
    if(!end_operation(gpu,&saved,ok)) return false;
    gpu->counts.dispatch_submit_ns+=now_ns()-start;
    return true;
}

static bool is_tiled_raster(const Job *job) {
    return job->w[OP]==TRI || job->w[OP]==REPLAY_RECT;
}

/* Same packed-word schedule as the standalone tiled replay: one workgroup
 * per occupied tile, one owning lane per word, jobs in ascending order. */
static void build_tile_schedule(TileSchedule *schedule,const PackedWordRectangle *bounds,unsigned count) {
    schedule->count=0;
    for(unsigned i=0;i<count;++i) {
        const PackedWordRectangle r=bounds[i];
        for(unsigned ty=r.top/TILE_HEIGHT;ty<=(r.bottom-1)/TILE_HEIGHT;++ty) {
            for(unsigned tx=r.left/TILE_WORD_WIDTH;tx<=(r.right-1)/TILE_WORD_WIDTH;++tx) {
                const unsigned tile=ty*TILE_COLUMNS+tx;
                if(!schedule->masks[tile]) schedule->entries[2u*schedule->count++]=tile;
                schedule->masks[tile]|=UINT32_C(1)<<i;
            }
        }
    }
    for(unsigned i=0;i<schedule->count;++i) {
        const unsigned tile=schedule->entries[2u*i];
        schedule->entries[2u*i+1]=schedule->masks[tile];
        schedule->masks[tile]=0;
    }
}

static bool emit_native_ot(Gpu *gpu,const ReplayNativeOt *ot) {
    const Job *jobs=ot->plan.jobs;
    const unsigned total=ot->plan.count;
    for(unsigned first=0;first<total;) {
        unsigned end=first+1;
        if(!is_tiled_raster(&jobs[first])) {
            /* No transfer enters a tile batch. Keep original upload SRC indices
             * and the original per-dispatch barrier for each overlapping copy row. */
            while(end<total && !is_tiled_raster(&jobs[end])) ++end;
            if(!emit_raster(gpu,jobs+first,end-first,ot->words,ot->word_count,NULL)) return false;
        } else {
            PackedWordRectangle bounds[MAX_BATCH_JOBS]={destination_words(&jobs[first])};
            while(end<total && is_tiled_raster(&jobs[end])) {
                const unsigned count=end-first;
                if(count==MAX_BATCH_JOBS) {
                    ++gpu->counts.capacity_splits;
                    break;
                }
                const PackedWordRectangle destination=destination_words(&jobs[end]);
                unsigned i=0;
                /* Test RAW and WAR against every earlier member, not just the last job. */
                for(;i<count;++i) {
                    if(texture_reads_intersect(&jobs[end],bounds[i]) ||
                       texture_reads_intersect(&jobs[first+i],destination)) break;
                }
                if(i!=count) {
                    ++gpu->counts.hazard_splits;
                    break;
                }
                bounds[count]=destination;
                ++end;
            }
            const unsigned count=end-first;
            if(count>gpu->counts.peak_batch_jobs) gpu->counts.peak_batch_jobs=count;
            TileSchedule *schedule=count>1 ? &gpu->tiles : NULL;
            if(schedule) build_tile_schedule(schedule,bounds,count);
            if(!emit_raster(gpu,jobs+first,count,NULL,0,schedule)) return false;
        }
        first=end;
    }
    return true;
}

bool race_gpu_mesa_submit_ot(Gpu *gpu,const ReplayNativeOt *ot) {
    if(!gpu || !gpu->seeded || !ot || ot->plan.count>MAX_JOBS || ot->word_count>MAX_WORDS) return false;
    if(!ot->plan.count) return true;
    SavedGraphicsState saved;
    if(!begin_operation(gpu,&saved)) return false;
    uint64_t start=now_ns();
    bind_raster(gpu);
    /* One borrowed-context save/restore per OT. Complete the OT's submissions
     * before returning to native ENV, STORE, scanout, or handoff. */
    bool ok=emit_native_ot(gpu,ot);
    if(!end_operation(gpu,&saved,ok)) return false;
    gpu->counts.dispatch_submit_ns+=now_ns()-start;
    return true;
}
bool race_gpu_mesa_gather(void *opaque,unsigned x,unsigned y,unsigned w,unsigned h,uint8_t *padded) {
    Gpu *gpu=opaque;
    SavedGraphicsState saved;
    if(!gpu || !gpu->seeded || !padded || x>=1024 || y>=512 || !w || w>16 || !h || h>16 ||
        h>512-y || !begin_operation(gpu,&saved)) return false;
    const unsigned pairs=(w+1)/2;
    uint64_t start=now_ns();
    prepare_exact_draw(gpu,gpu->store_framebuffer,pairs,h);
    glUseProgram(gpu->store_program);
    glUniform3i(gpu->store_rectangle,x,y,w);
    glDrawArrays(GL_TRIANGLES,0,3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,gpu->store_framebuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    for(unsigned i=0;i<COUNT(pack_fields);++i) glPixelStorei(pack_fields[i],i==0?1:0);
    bool ok=check_gl("STORE draw and pack setup");
    uint64_t read_start=now_ns();
    if(ok) {
        glReadPixels(0,0,pairs,h,GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,padded);
        ok=check_gl("STORE read pixels");
    }
    uint64_t restore_start=now_ns();
    if(!end_operation(gpu,&saved,ok)) return false;
    ++gpu->counts.store_reads; gpu->counts.store_bytes+=w*h*2u;
    gpu->counts.store_submit_ns+=read_start-start; gpu->counts.store_wait_ns+=restore_start-read_start;
    gpu->counts.store_restore_ns+=now_ns()-restore_start;
    return true;
}
unsigned race_gpu_mesa_scanout(Gpu *gpu,int x,int y,int w,int h,int cx,int cy,int cw,int ch,int dx,int dy) {
    SavedGraphicsState saved;
    if(!gpu || !gpu->seeded || x<0 || x>=1024 || y<0 || y>=512 || w<=0 || w>1024-x || h<=0 || h>512-y ||
        cx<0 || cx>=w || cy<0 || cy>=h || cw<=0 || cw>w-cx || ch<=0 || ch>h-cy ||
        dx < -1024 || dx > 1024 || dy < -512 || dy > 512 || !begin_operation(gpu,&saved)) return 0;
    uint64_t start=now_ns();
    prepare_exact_draw(gpu,gpu->scan_fbo,cw,ch);
    if(gpu->scan_width!=cw || gpu->scan_height!=ch) {
        /* TexStorage is immutable; replace the allocation via TexImage with no client data.
         * The pack/unpack bindings are independent, and a null pointer is data when unpack is bound. */
        GLint unpack=0; glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glBindTexture(GL_TEXTURE_2D,gpu->scan_texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,cw,ch,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,gpu->scan_texture,0);
        gpu->scan_width=cw; gpu->scan_height=ch;
    }
    bool ok=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(ok) {
        glUseProgram(gpu->scan_program);
        if(gpu->perspective) glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,gpu->buffers[DISPLAY_OVERRIDE_BUFFER]);
        glUniform4i(gpu->display_rect,x,y,w,h);
        glUniform2i(gpu->crop_origin,cx,cy);
        glUniform2i(gpu->display_offset,dx,dy);
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    bool clean=check_gl("VRAM scanout");
    if(!end_operation(gpu,&saved,ok && clean)) return 0;
    ++gpu->counts.scanouts; gpu->counts.scanout_submit_ns+=now_ns()-start;
    return gpu->scan_texture;
}
const uint32_t *race_gpu_mesa_stage_boundary(Gpu *gpu) {
    SavedGraphicsState saved;
    if(!gpu || !gpu->seeded || !begin_operation(gpu,&saved)) return NULL;
    gpu->drained=true;
    gpu->boundary=malloc(VRAM_BYTES);
    bool ok=false;
    if(gpu->boundary) {
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glBindBuffer(GL_COPY_READ_BUFFER,gpu->buffers[VRAM_BUFFER]);
        glBindBuffer(GL_COPY_WRITE_BUFFER,gpu->buffers[STAGING_BUFFER]);
        glBufferData(GL_COPY_WRITE_BUFFER,VRAM_BYTES,NULL,GL_STREAM_READ);
        glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,VRAM_BYTES);
        if(check_gl("boundary staging copy")) {
            glFinish(); /* exceptional handoff only, never scanout */
            const void *mapped=glMapBufferRange(GL_COPY_WRITE_BUFFER,0,VRAM_BYTES,GL_MAP_READ_BIT);
            if(mapped) {
                memcpy(gpu->boundary,mapped,VRAM_BYTES);
                ok=glUnmapBuffer(GL_COPY_WRITE_BUFFER)==GL_TRUE;
            }
            bool clean=check_gl("boundary staging map"); ok=ok && clean;
        }
    }
    if(!end_operation(gpu,&saved,ok)) return NULL;
    gpu->counts.boundary_bytes=VRAM_BYTES;
    return gpu->boundary;
}
RaceGpuMesaCounters race_gpu_mesa_counters(const Gpu *gpu) { return gpu->counts; }
