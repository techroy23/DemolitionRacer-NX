#include <assert.h>
#include "resume_diag.h"

int main(void)
{
    ResumeTrace trace = {0};
    resume_trace_span(&trace, RESUME_SWAP, 10, 49999, 0, false);
    assert(trace.total == 0);
    resume_trace_span(&trace, RESUME_SWAP, 10, 50010, 0x1234, false);
    assert(trace.total == 1);
    const ResumeEvent *first = resume_trace_get(&trace, 0);
    assert(first && first->stage == RESUME_SWAP && first->begin == 10 && first->end == 50010 && first->value == 0x1234);
    for (unsigned i = 0; i <= RESUME_CAPACITY; ++i)
        resume_trace_span(&trace, RESUME_HOOK, i, i, i, true);
    assert(resume_trace_get(&trace, 0)->value == 1);
    assert(resume_trace_get(&trace, RESUME_CAPACITY - 1)->value == RESUME_CAPACITY);
    assert(!resume_trace_get(&trace, RESUME_CAPACITY));
    return 0;
}
