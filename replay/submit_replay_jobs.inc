/* Shared by the Windows OpenGL and Switch Mesa GLES backends. */
int gpu_replay(Gpu *gpu,const Job *jobs,unsigned job_count,const uint32_t *words,
               size_t word_count,const TileSchedule *schedule){
    if(!gpu || job_count>MAX_JOBS || word_count>MAX_WORDS || (!words && word_count))return 0;
    if(schedule && (job_count<2 || job_count>MAX_BATCH_JOBS || word_count ||
                    !schedule->count || schedule->count>TILE_COUNT))return 0;
    if(!job_count)return 1;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,gpu->buffers[JOB_BUFFER]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,job_count*sizeof(Job),jobs,GL_DYNAMIC_DRAW);
    const uint32_t empty_input=0;
    if(schedule){words=schedule->entries;word_count=2u*schedule->count;}
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,gpu->buffers[INPUT_BUFFER]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,word_count?word_count*sizeof(*words):sizeof(empty_input),
                 word_count?words:&empty_input,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,gpu->buffers[INPUT_BUFFER]);
    if(!check_gl("job upload"))return 0;
    if(schedule){
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,gpu->buffers[JOB_BUFFER]);
        glUniform1i(gpu->batch_configuration,1);
        glDispatchCompute(schedule->count,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glUniform1i(gpu->batch_configuration,0);
    }else{
        for(unsigned i=0;i<job_count;++i){
            const uint32_t *job=jobs[i].w;
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER,1,gpu->buffers[JOB_BUFFER],i*sizeof(Job),sizeof(Job));
            unsigned groups=job[OP]==COPY_ROW?1u:(job[HX]-(job[LX]&~1u)+63u)/64u;
            glDispatchCompute(groups,job[HY]-job[LY],1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }
    return check_gl("dispatch");
}
