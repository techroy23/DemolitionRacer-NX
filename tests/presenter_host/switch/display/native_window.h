#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
typedef int Result;
#define R_FAILED(result) ((result) != 0)
typedef struct {
    u32 width, height, slots_configured, format, usage, swap_interval, transform, sticky_transform;
    struct { int left, top, right, bottom; } crop;
} NWindow;
static NWindow test_window;
static inline NWindow *nwindowGetDefault(void) { return &test_window; }
static inline bool nwindowIsValid(NWindow *w) { return w == &test_window; }
static inline Result nwindowGetDimensions(NWindow *w, u32 *x, u32 *y) { *x=w->width; *y=w->height; return 0; }
static inline Result nwindowSetDimensions(NWindow *w,u32 x,u32 y) { w->width=x; w->height=y; return 0; }
static inline Result nwindowSetSwapInterval(NWindow *w,u32 n) { w->swap_interval=n; return 0; }
