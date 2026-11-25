/* Shared by the standalone WGL and GLES replay backends. The replay owns
 * the context: default FBO/VAO and no pack buffer outside this pass. */
static const char store_vertex_source[] =
    "#version 310 es\n"
    "void main() {\n"
    "    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char store_fragment_source[] =
    "#version 310 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "layout(std430, binding=0) readonly buffer Vram { uint vram[]; };\n"
    "uniform ivec3 storeRectangle;\n"  /* normalized source x, source y, width */
    "layout(location=0) out uvec4 storeBytes;\n"
    "uint pixelAt(uint x, uint y) {\n"
    "    uint index = y * 1024u + (x & 1023u);\n"
    "    return (vram[index >> 1u] >> ((index & 1u) * 16u)) & 65535u;\n"
    "}\n"
    "void main() {\n"
    "    uint column = uint(gl_FragCoord.x) * 2u;\n"
    "    uint sourceRow = uint(storeRectangle.y) + uint(gl_FragCoord.y);\n"
    "    uint sourceX = uint(storeRectangle.x) + column;\n"
    "    uint first = pixelAt(sourceX, sourceRow);\n"
    "    uint second = column + 1u < uint(storeRectangle.z) ? pixelAt(sourceX + 1u, sourceRow) : 0u;\n"
    "    storeBytes = uvec4(first & 255u, first >> 8u, second & 255u, second >> 8u);\n"
    "}\n";

static GLuint compile_store_shader(GLenum stage, const char *source){
    GLuint shader=glCreateShader(stage);
    if (!shader) return 0;
    glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);
    GLint compiled=0;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
    if (!compiled) {
        char log[2048]={0};
        glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        report("STORE shader 0x%x compilation failed: %s\n",(unsigned)stage,log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void close_store_fbo(Gpu *gpu){
    if (gpu->store_framebuffer) glDeleteFramebuffers(1,&gpu->store_framebuffer);
    if (gpu->store_color) glDeleteRenderbuffers(1,&gpu->store_color);
    if (gpu->store_vao) glDeleteVertexArrays(1,&gpu->store_vao);
    if (gpu->store_program) glDeleteProgram(gpu->store_program);
}

static int open_store_fbo(Gpu *gpu){
    GLint fragment_blocks=0, max_block_bytes=0;
    glGetIntegerv(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS,&fragment_blocks);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&max_block_bytes);
    if (!check_gl("STORE SSBO limits")) return 0;
    report("STORE caps fragment_ssbo_blocks=%d max_ssbo_bytes=%d\n",fragment_blocks,max_block_bytes);
    if (fragment_blocks<1 || max_block_bytes<(GLint)VRAM_BYTES) {
        report("STORE requires one fragment SSBO block of at least %u bytes; no fallback\n",VRAM_BYTES);
        return 0;
    }
    GLuint vertex_shader=compile_store_shader(GL_VERTEX_SHADER,store_vertex_source);
    if (!vertex_shader) return 0;
    GLuint fragment_shader=compile_store_shader(GL_FRAGMENT_SHADER,store_fragment_source);
    if (!fragment_shader) {glDeleteShader(vertex_shader);return 0;}
    gpu->store_program=glCreateProgram();
    if (gpu->store_program) {
        glAttachShader(gpu->store_program,vertex_shader);
        glAttachShader(gpu->store_program,fragment_shader);
        glLinkProgram(gpu->store_program);
    }
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    if (!gpu->store_program) return 0;
    GLint linked=0;
    glGetProgramiv(gpu->store_program,GL_LINK_STATUS,&linked);
    if (!linked) {
        char log[2048]={0};
        glGetProgramInfoLog(gpu->store_program,sizeof(log),NULL,log);
        report("STORE program linking failed: %s\n",log);
        return 0;
    }
    gpu->store_rectangle=glGetUniformLocation(gpu->store_program,"storeRectangle");
    if (gpu->store_rectangle<0) {
        report("STORE rectangle uniform unavailable\n");
        return 0;
    }
    glGenVertexArrays(1,&gpu->store_vao);
    glGenRenderbuffers(1,&gpu->store_color);
    glBindRenderbuffer(GL_RENDERBUFFER,gpu->store_color);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8UI,STORE_FBO_WIDTH,STORE_FBO_HEIGHT);
    glGenFramebuffers(1,&gpu->store_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,gpu->store_framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,gpu->store_color);
    const GLenum color_attachment=GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1,&color_attachment);
    glReadBuffer(color_attachment);
    if (!check_gl("STORE FBO setup") || !gpu->store_vao || !gpu->store_color || !gpu->store_framebuffer) return 0;
    const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    report("STORE FBO RGBA8UI %ux%u status=0x%04x\n",STORE_FBO_WIDTH,STORE_FBO_HEIGHT,(unsigned)status);
    if (status!=GL_FRAMEBUFFER_COMPLETE) return 0;
#if defined(__SWITCH__) || defined(RACE_GPU_SHARED_CONTEXT)
    /* ES guarantees RGBA_INTEGER/UNSIGNED_INT, not the byte pair. Check
     * the preferred pair with the actual read attachment bound. */
    GLint read_format=0, read_type=0;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT,&read_format);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE,&read_type);
    if (!check_gl("STORE read format query")) return 0;
    report("STORE implementation read format=0x%04x type=0x%04x\n",(unsigned)read_format,(unsigned)read_type);
    if (read_format!=GL_RGBA_INTEGER || read_type!=GL_UNSIGNED_BYTE) {
        report("STORE GLES does not advertise RGBA_INTEGER/UNSIGNED_BYTE for RGBA8UI; no fallback\n");
        return 0;
    }
#endif
    glBindRenderbuffer(GL_RENDERBUFFER,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glGetIntegerv(GL_VIEWPORT,gpu->replay_viewport);
    glDisable(GL_DITHER);
    return check_gl("STORE initialization");
}

#ifndef RACE_GPU_SHARED_CONTEXT
int gpu_read_store_fbo(Gpu *gpu, unsigned x, unsigned y, unsigned width, unsigned height,
                       void *destination, StoreFboTiming *timing){
    if (x>=1024u || y>=512u || !width || width>STORE_MAX_PIXELS_PER_ROW ||
        !height || height>STORE_FBO_HEIGHT || height>512u-y) return 0;
    const unsigned pair_columns=(width+1u)/2u;
    unsigned char padded_rows[STORE_FBO_CAPACITY];
    const uint64_t start_ns=replay_now_ns();
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER,gpu->store_framebuffer);
    glViewport(0,0,pair_columns,height);
    glUseProgram(gpu->store_program);
    glUniform3i(gpu->store_rectangle,x,y,width);
    glBindVertexArray(gpu->store_vao);
    glDrawArrays(GL_TRIANGLES,0,3);
    const int draw_ok=check_gl("STORE fragment draw");
    const uint64_t submitted_ns=replay_now_ns();
    int read_ok=0;
    if (draw_ok) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        /* FBO y=0 represents the first PS1 rectangle row; ReadPixels
         * returns the lowest FBO row first, so no CPU flip is needed. */
        glReadPixels(0,0,pair_columns,height,GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,
                     (width&1u)?padded_rows:destination);
        read_ok=check_gl("STORE integer ReadPixels");
    }
    const uint64_t read_ns=replay_now_ns();
    glPixelStorei(GL_PACK_ALIGNMENT,4);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(gpu->replay_viewport[0],gpu->replay_viewport[1],gpu->replay_viewport[2],gpu->replay_viewport[3]);
    glUseProgram(gpu->program);
    if (!check_gl("STORE restore compute state") || !read_ok) return 0;
    if (width&1u) {
        const size_t row_bytes=(size_t)width*2u;
        for (unsigned row=0;row<height;++row)
            memcpy((unsigned char *)destination+row*row_bytes,padded_rows+row*pair_columns*4u,row_bytes);
    }
    *timing=(StoreFboTiming){submitted_ns-start_ns,read_ns-submitted_ns,replay_now_ns()-read_ns};
    return 1;
}
#endif
