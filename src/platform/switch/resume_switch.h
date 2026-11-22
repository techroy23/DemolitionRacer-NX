#ifndef DRACER_RESUME_SWITCH_H
#define DRACER_RESUME_SWITCH_H

#ifdef DRACER_RESUME_TRACE
#include <stdio.h>
#include "resume_diag.h"

void resume_switch_start(void);
void resume_switch_stop(FILE *log);
void resume_switch_probe(bool force);
uint64_t resume_switch_clock(uint64_t raw_us);
uint64_t resume_switch_now(void);
void resume_switch_end(ResumeStage stage, uint64_t begin, uint64_t value, bool force);
void resume_switch_audio_drop(void);

#define RESUME_BEGIN(id) const uint64_t resume_start_##id = resume_switch_now()
#define RESUME_END(stage, id, value, force) resume_switch_end(stage, resume_start_##id, value, force)
#else
#define RESUME_BEGIN(id) ((void)0)
#define RESUME_END(stage, id, value, force) ((void)0)
#endif

#endif
