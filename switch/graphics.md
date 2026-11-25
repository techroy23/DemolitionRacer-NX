# Switch video output

## Default path (v0.2.5)

The port prefers EGL/GLES for presentation outside applet mode. It uploads finished CPU frames or presents a GPU texture from the solo race renderer, preserving the centered 4:3 race viewport. At `x4`, handheld uses 1280×720 output with a 960×720 game viewport; docked uses 1920×1080 output with a 1280×960 game viewport. On an operation-mode change, it closes the old EGL producer before trying to rebuild for the new output. Applet mode and recoverable presentation failures can use the libnx framebuffer.

## Solo race renderer

GPU rasterization with perspective correction is automatic in eligible solo races. No settings changes or marker files are required, and any old `.enable` files are ignored. The path requires an active GLES presenter and `CAR_LOD` and `TRACK_DISTANCE` at their `STOCK` defaults. Menus, movies, loading screens, split-screen, demos, replay mode, and unsupported render states use stock rasterization. There is no new renderer switch.

Recoverable failures keep or restore the stock path. If the renderer cannot guarantee correct VRAM contents, it stops rather than retrying a possibly partial GPU draw on the CPU.

Perspective correction uses projection depth for eligible textured track triangles and quads, including near subdivision leaves and unsplit near parents. Invalid or saturated projections remain affine. Corrected texels affect only the displayed image: texture feedback, game-visible VRAM reads, and handoff to the stock renderer retain stock affine values. This does not change physics, the camera, or draw distance.

Use `LOG: INFO` or `DEBUG` and check the dated log for race eligibility, platform readiness, actual GPU ownership, any stock handoff, and `borrowed-texture` counts. Submissions do not measure scanout or corrected pixels. For a stock comparison with the same route, camera, and settings, use v0.2.4. See the [OLED evidence](../PLAN.MD#v025-release-evidence) for what was tested.

## Original framebuffer path

Version 0.1.2 introduced libnx's linear framebuffer. Both STR movies and game rendering send CPU-generated pixels through `xport_present`. The software path copies those pixels to BGRA8888 with opaque alpha. See the [libnx framebuffer API](https://switchbrew.github.io/libnx/framebuffer_8h.html) and [simplegfx example](https://github.com/switchbrew/switch-examples/tree/master/graphics/simplegfx).

On hardware, 0.1.1 played audio and read menu assets but remained black. The old success log was not proof of a buffer swap: `vid_blit` returned success even when the calculated output rectangle was empty. The exact device-side failure is still unknown. The 0.1.2 build bypassed EGL and logged source dimensions, sampled colors, and submission counts. A submission is not confirmation that the display showed the frame.

## Historical EGL/GLES prototype (not built)

`vid_ogl.patch` targets xport `5b304d489388d9e3ef2fb6e899f682920103d10b`.
Apply it to a fresh build copy of `src/vid_ogl.c`, not to the dependency
checkout. From the project root in Git Bash:

```sh
root=$(pwd -W)
mkdir -p build/switch-vid/src
cp src/xport/src/vid_ogl.c build/switch-vid/src/
git apply --ignore-space-change --directory=build/switch-vid switch/vid_ogl.patch
```

`--ignore-space-change` allows the LF patch to match the pinned CRLF source.
Compile the generated `build/switch-vid/src/vid_ogl.c` **instead of** the original
and add `-I"$root/src/xport/src"` for its xport headers. Use the normal
libnx Switch flags (including `-D__SWITCH__`), libnx and Switch portlibs include
paths, and `-DAP_SWITCH -DGAPI_OGL -DXPORT_NATIVE -DFIELD_RATE=60` plus the
project's `WND_TITLE`, `WND_WIDTH`, and `WND_HEIGHT` definitions. Do not define
`AP_WEB`.

With `aarch64-none-elf-gcc` and libnx `switch.specs`, link the following after
the objects, using the Switch portlibs and libnx library paths:

```text
-lEGL -lGLESv2 -lglapi -ldrm_nouveau -lstdc++ -lnx -lm
```

Mesa exports GLES 3 entry points from `libGLESv2`; no GL loader is needed.
`vid_init(NULL)` acquires libnx's default `NWindow` and makes the EGL context
current. A non-NULL window must be an `NWindow *` that outlives the renderer.
Initialize, render, and call `vid_free()` on the same thread. Do not run the
libnx console or a separate framebuffer renderer on that window concurrently.
`vid_blit()` and presenting `vid_end()` calls swap internally; offscreen passes
do not swap. The platform should not issue an additional swap.
