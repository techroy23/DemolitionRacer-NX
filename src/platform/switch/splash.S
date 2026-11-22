.section .rodata.dracer_splash,"a",%progbits
.balign 16
.global dracer_splash_pixels
.type dracer_splash_pixels, %object
dracer_splash_pixels:
#ifndef DRACER_SPLASH_FILE
#define DRACER_SPLASH_FILE "build/game_splash.bgra"
#endif
.incbin DRACER_SPLASH_FILE
.size dracer_splash_pixels, . - dracer_splash_pixels
