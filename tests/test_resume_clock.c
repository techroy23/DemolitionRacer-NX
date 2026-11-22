#include <assert.h>
#include "resume_clock.h"

int main(void)
{
    ResumeClock clock;
    resume_clock_init(&clock, true, 100, true, 10000);
    assert(resume_clock_sample(&clock, 16783, true, 26683) == 16683);
    /* About five seconds of raw HOME time but only one active field. */
    assert(resume_clock_sample(&clock, 5033466, true, 43366) == 33366);
    assert(resume_clock_sample(&clock, 5034000, true, 42000) == 33366);
    assert(resume_clock_sample(&clock, 5035000, true, 44900) == 34900);
    /* A failed service call must not inject the preceding raw gap. */
    assert(resume_clock_sample(&clock, 9000000, false, 0) == 34900);
    assert(!clock.active);
    assert(resume_clock_sample(&clock, 9001000, true, 999999) == 35900);
    resume_clock_init(&clock, false, 0, true, 10000);
    assert(resume_clock_sample(&clock, 5000000, true, 10001) == 5000000);
    resume_clock_init(&clock, true, 10, false, 0);
    assert(resume_clock_sample(&clock, 30, true, 50000) == 20);
    return 0;
}
