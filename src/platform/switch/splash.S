.section .rodata.dracer_splash,"a",%progbits
.balign 16
.global dracer_splash_pixels
.type dracer_splash_pixels, %object
dracer_splash_pixels:
.incbin "build/game_splash.bgra"
.size dracer_splash_pixels, . - dracer_splash_pixels
