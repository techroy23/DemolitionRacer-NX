#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define W 9
#define H 5
static const char vertex[] = "#version 310 es\nout highp vec2 uv;\n"
    "void main(){vec2 c=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
    "gl_Position=vec4(2.*c-1.,0.,1.);uv=vec2(c.x,1.-c.y);}";
static const char composite[] = "#version 310 es\nprecision highp float;\n"
    "uniform sampler2D pixels; in vec2 uv; out vec4 color;"
    "void main(){color=vec4(texture(pixels,uv).bgr,1.);}";
static const char compute[] = "#version 310 es\nprecision highp int;\n"
    "layout(local_size_x=64) in; layout(std430,binding=0) buffer V { uint words[]; }; uniform uint pass;"
    "uint p(uint i){return (23u*(i%1024u)+907u*(i/1024u)+1009u*pass)^32768u;}"
    "void main(){uint w=gl_GlobalInvocationID.x;words[w]=(p(2u*w)&65535u)|(p(2u*w+1u)<<16u);}";

static GLuint program(GLenum stage, const char *src, const char *fragment)
{
    const char *sources[2]={src,fragment};
    GLenum stages[2]={stage,GL_FRAGMENT_SHADER}; GLuint p=glCreateProgram();
    for (unsigned i=0;i<(fragment?2u:1u);++i) {
        GLuint sh=glCreateShader(stages[i]); GLint ok=0; char log[2048]={0};
        glShaderSource(sh,1,&sources[i],NULL); glCompileShader(sh); glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
        if (!ok) { glGetShaderInfoLog(sh,sizeof log,NULL,log); fprintf(stderr,"%s\n",log); abort(); }
        glAttachShader(p,sh); glDeleteShader(sh);
    }
    glLinkProgram(p); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if (!ok) { char log[2048]={0}; glGetProgramInfoLog(p,sizeof log,NULL,log); fprintf(stderr,"%s\n",log); abort(); }
    return p;
}
static uint8_t expand(unsigned v) { v &= 31u; return (uint8_t)((v<<3)|(v>>2)); }
int main(void)
{
    const EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    const EGLint context_attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
    const EGLint surf_attrs[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    const char *backend=getenv("DRACER_ANGLE_BACKEND");
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (backend) {
        /* ANGLE: select OpenGL (0x320D) or Vulkan (0x3450) explicitly. */
        const EGLAttrib attrs[]={0x3203,strtol(backend,NULL,0),EGL_NONE};
        d=eglGetPlatformDisplay(0x3202,EGL_DEFAULT_DISPLAY,attrs);
    }
    EGLConfig cfg; EGLint count;
    assert(eglInitialize(d,NULL,NULL)); assert(eglBindAPI(EGL_OPENGL_ES_API));
    assert(eglChooseConfig(d,config_attrs,&cfg,1,&count) && count==1);
    EGLContext ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,context_attrs);
    EGLSurface surf=eglCreatePbufferSurface(d,cfg,surf_attrs);
    assert(ctx!=EGL_NO_CONTEXT && surf!=EGL_NO_SURFACE && eglMakeCurrent(d,surf,surf,ctx));
    printf("%s / %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER)); fflush(stdout);
    FILE *f=fopen("src/platform/switch/race_gpu_scanout.frag","rb");
    if (!f) { perror("scanout shader"); return 1; }
    char frag[4096]; size_t n=fread(frag,1,sizeof frag-1,f); assert(feof(f) && !ferror(f)); fclose(f); frag[n]=0;
    GLuint convert=program(GL_VERTEX_SHADER,vertex,frag), present=program(GL_VERTEX_SHADER,vertex,composite);
    GLuint writer=program(GL_COMPUTE_SHADER,compute,NULL), ssbo, tex, fbo, vao;
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenBuffers(1,&ssbo); glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,1024*512*2,NULL,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,ssbo);
    glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,W,H);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glDisable(GL_DITHER);
    for (unsigned pass=0;pass<4;++pass) {
        int bank=pass%2 ? 264 : 8;
        int crop_x=pass<2 ? 3 : 0, crop_y=pass<2 ? 2 : 0;
        int offset_x=pass<2 ? 0 : 2, offset_y=pass<2 ? 0 : 1;
        glUseProgram(writer); glUniform1ui(glGetUniformLocation(writer,"pass"),pass);
        glDispatchCompute(4096,1,1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo); glViewport(0,0,W,H); glUseProgram(convert);
        glUniform4i(glGetUniformLocation(convert,"displayRect"),0,bank,320,240);
        glUniform2i(glGetUniformLocation(convert,"cropOrigin"),crop_x,crop_y);
        glUniform2i(glGetUniformLocation(convert,"displayOffset"),offset_x,offset_y);
        glDrawArrays(GL_TRIANGLES,0,3);
        glBindFramebuffer(GL_FRAMEBUFFER,0); glUseProgram(present); glDrawArrays(GL_TRIANGLES,0,3);
        uint8_t result[H][W][4]; glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,result);
        assert(glGetError()==GL_NO_ERROR);
        for (int y=0;y<H;++y) for(int x=0;x<W;++x) {
            int sx=x+crop_x-offset_x, sy=(H-1-y)+crop_y-offset_y;
            uint16_t p=sx<0||sy<0 ? 0 : (uint16_t)((23u*sx+907u*(bank+sy)+1009u*pass)^32768u);
            assert(result[y][x][0]==expand(p) && result[y][x][1]==expand(p>>5));
            assert(result[y][x][2]==expand(p>>10) && result[y][x][3]==255);
        }
    }
    assert(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    assert(eglDestroySurface(d,surf) && eglDestroyContext(d,ctx) && eglTerminate(d));
    puts("race_gpu_scanout: 4 GPU-written cases, both bank origins, crop, offsets and stock composite passed");
    return 0;
}
