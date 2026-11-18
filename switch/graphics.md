# Switch EGL/GLES renderer

`vid_ogl.patch` targets xport `5b304d489388d9e3ef2fb6e899f682920103d10b`.
Apply it to a fresh build copy of `src/vid_ogl.c`, not to the dependency
checkout. From the project root in Git Bash:

```sh
root=$(pwd -W)
mkdir -p build/switch-vid/src
cp .deps/xport/src/vid_ogl.c build/switch-vid/src/
git apply --ignore-space-change --directory=build/switch-vid switch/vid_ogl.patch
```

`--ignore-space-change` allows the LF patch to match the pinned CRLF source.
Compile the generated `build/switch-vid/src/vid_ogl.c` **instead of** the original
and add `-I"$root/.deps/xport/src"` for its xport headers. Use the normal
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
