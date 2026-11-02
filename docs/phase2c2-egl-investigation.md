# Phase 2C.2 Mesa EGL lifecycle investigation

## Finding

**The measured lifecycle owner is wlroots, not Seyrix.** Mesa allocates all five LeakSanitizer groups during EGL DRI2 initialization. wlroots creates and destroys the EGL context and renderer, but in this runtime it skips eglTerminate because EGL_KHR_display_reference is unavailable. Calling eglTerminate in the direct EGL/GBM control releases the Mesa allocations; omitting it in a minimal wlroots renderer produces them, and five renderer lifecycles produce exactly five times the one-cycle leak. The full Seyrix process matches the one-cycle wlroots result.

This identifies the retained resource and the layer whose teardown policy leaves it behind. It does not establish whether wlroots intends this to be process-lifetime retention when EGL_KHR_display_reference is absent or whether upstream considers it a defect. Mesa owns the heap objects and frees them when the display is terminated; the evidence does not show the same leak in Mesa after a properly terminated EGL display. No Seyrix-owned EGL object or missing Seyrix destroy call was found. No production code was changed.

## Initial EGL/GBM control failure

The original helper failed at **eglInitialize(display, NULL, NULL)**. The preserved output was:

~~~text
EGL_CONTROL_LIBRARY=/usr/lib/libEGL.so.1
EGL_CONTROL_INITIALIZE=failed egl_error=0x3000
EGL_CONTROL_EXIT=1
~~~

The call returned EGL_FALSE. The immediately retrieved error was EGL_SUCCESS (0x3000), so Mesa did not provide a useful EGL error. The original source did not print the display handle. Control flow establishes that eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, gbm, NULL) had returned a non-EGL_NO_DISPLAY value and gbm_create_device had succeeded; otherwise the helper would have exited through earlier diagnostic branches. The handle's numeric value was not recorded and cannot be recovered from the preserved log. The helper opened /dev/dri/renderD128 directly, ran as root, and did not set the compositor's software-rendering environment.

The guest GPU was /dev/dri/card1, driver virtio_gpu, PCI 1af4:1050. In the corrected control, libdrm resolved card1 to /dev/dri/renderD128, confirming that the old hard-coded path happened to name this guest's render node. The compositor's EGL client extension list in the old run included EGL_EXT_platform_base, EGL_KHR_platform_gbm, EGL_MESA_platform_gbm, EGL_EXT_platform_device, and EGL_MESA_platform_surfaceless. The corrected run also confirmed EGL_KHR_no_config_context, EGL_MESA_configless_context, and EGL_KHR_surfaceless_context on the display.

The corrected helper now follows the compositor's device-selection path: open the primary DRM node, resolve its render node with libdrm, create GBM on that node, and request EGL_PLATFORM_GBM_KHR. It runs as the Seyrix session user with LIBSEAT_BACKEND=logind, LIBGL_ALWAYS_SOFTWARE=1, and WLR_RENDERER_ALLOW_SOFTWARE=1. It queries and records the client/display extensions, creates a configless surfaceless GLES context, checks the actual GL renderer, and performs explicit teardown. It succeeded on the same guest and reported llvmpipe.

The old helper's setup was not equivalent to Seyrix, and the repaired path succeeds. The old run did not isolate which difference caused eglInitialize to return false with EGL_SUCCESS. The missing forced-software environment is the strongest remaining explanation, but it is a hypothesis rather than a demonstrated single cause. The later config/pbuffer setup cannot explain the failure because initialization failed before those calls.

Detailed initial failure evidence: [initial-control-failure.txt](evidence/phase2c2/egl-lifecycle/initial-control-failure.txt), [initial QEMU command](evidence/phase2c2/egl-lifecycle/initial-failure-qemu-command.txt), and [initial run metadata](evidence/phase2c2/egl-lifecycle/initial-failure-run-metadata.txt).

## Three-case comparison

All three cases used the same sanitizer ISO and guest graphics stack. Runtime renderer identity was read from GL_RENDERER or wlroots' renderer log, not inferred from environment variables.

| Case | Lifecycle | Actual renderer | LSan result | Process status |
| --- | --- | --- | --- | ---: |
| Direct Mesa EGL/GBM, 1 cycle | GBM display, configless surfaceless GLES2 context, eglTerminate, eglReleaseThread, GBM/fd cleanup | llvmpipe (LLVM 23.1.3, 128 bits) | No leak report | 0 |
| Direct Mesa EGL/GBM, 5 cycles | Same sequence repeated five times | llvmpipe (LLVM 23.1.3, 128 bits) | No leak report | 0 |
| Minimal wlroots renderer, 1 cycle | wl_display, autocreated backend/renderer, renderer display init, renderer/backend/display destruction | llvmpipe (LLVM 23.1.3, 128 bits) | 31,259 bytes / 154 allocations | 1 |
| Minimal wlroots renderer, 5 cycles | Correctly create/destroy renderer and its display/backend five times in one process | llvmpipe (LLVM 23.1.3, 128 bits) | 156,295 bytes / 770 allocations | 1 |
| Full Seyrix compositor, 1 lifecycle | Start compositor, request graceful SIGTERM, wait for shutdown | llvmpipe (LLVM 23.1.3, 128 bits) | 31,259 bytes / 154 allocations | 1 |

The five-cycle wlroots result is exactly five times the one-cycle result in every allocation class:

| LSan class | 1 cycle | 5 cycles |
| --- | ---: | ---: |
| Direct Mesa display object | 368 bytes / 1 | 1,840 bytes / 5 |
| Direct Mesa config array | 32 bytes / 1 | 160 bytes / 5 |
| Indirect Mesa config objects | 28,800 bytes / 150 | 144,000 bytes / 750 |
| Indirect Mesa array storage | 2,048 bytes / 1 | 10,240 bytes / 5 |
| Indirect Mesa DRM string | 11 bytes / 1 | 55 bytes / 5 |
| **Total** | **31,259 bytes / 154** | **156,295 bytes / 770** |

These are LSan unreachable allocations at process exit. With wlroots, renderer destruction releases the EGL context and thread state but the EGL display is not terminated in this environment; the Mesa objects therefore accumulate across repeated renderer creation/destruction while the process remains alive. With direct EGL, eglTerminate succeeds and both one-cycle and five-cycle processes exit cleanly. The OS reclaims process memory after exit, but that is not renderer/display cleanup.

The full raw reports, including all sanitizer addresses, library Build IDs, and caller frames, are preserved in [wlroots-1.log](evidence/phase2c2/egl-lifecycle/wlroots-1.log), [wlroots-5.log](evidence/phase2c2/egl-lifecycle/wlroots-5.log), and [seyrix-compositor.log](evidence/phase2c2/egl-lifecycle/seyrix-compositor.log). Symbolized allocation frames and source locations are in [mesa-egl-wlroots-symbolized-stacks.txt](evidence/phase2c2/egl-lifecycle/mesa-egl-wlroots-symbolized-stacks.txt). The direct-control success logs and every process exit code are in the same evidence directory.

Exit-status files: [direct EGL, 1 cycle](evidence/phase2c2/egl-lifecycle/egl-direct-1.exit), [direct EGL, 5 cycles](evidence/phase2c2/egl-lifecycle/egl-direct-5.exit), [wlroots, 1 cycle](evidence/phase2c2/egl-lifecycle/wlroots-1.exit), [wlroots, 5 cycles](evidence/phase2c2/egl-lifecycle/wlroots-5.exit), and [Seyrix](evidence/phase2c2/egl-lifecycle/seyrix-compositor.exit).

## Allocation origin and cleanup responsibility

The Mesa Build ID was f5b4c5531e150775af105a811e633f65e15bbe4f. The wlroots Build ID was 2cd3a56136647a9da32b9a682606911aa2498393. Matching debug symbols were used with addr2line; the symbol files remain in the external investigation cache and were not copied into the repository.

The allocation groups resolve to:

- **368 bytes, one direct object:** Mesa dri2_display_create at src/egl/drivers/dri2/egl_dri2.c:1030, called from Mesa eglInitialize. This is the DRI2 display object.
- **32 bytes, one direct object:** Mesa _eglCreateArray and _eglLinkConfig while linking a DRM EGL config.
- **28,800 bytes, 150 indirect objects:** Mesa dri2_add_config at egl_dri2.c:463 while enumerating DRM visuals/configs.
- **2,048 bytes, one indirect object:** Mesa _eglGrowArray / _eglAppendArray while linking those configs.
- **11 bytes, one indirect string:** Mesa dri2_initialize_drm at platform_drm.c:682.

All five stacks enter Mesa from eglInitialize. Their shared caller is wlroots' egl_init_display → egl_init → wlr_egl_create_with_drm_fd → wlr_gles2_renderer_create_with_drm_fd → renderer_autocreate. This puts allocation creation in Mesa but does not make Mesa responsible for a missed cleanup call.

The wlroots 0.20.2 source at [render/egl.c](https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/0.20.2/render/egl.c#L648) makes the teardown rule explicit. wlr_egl_destroy unbinds and destroys the EGL context, calls eglTerminate only inside if (egl->exts.KHR_display_reference), calls eglReleaseThread, destroys GBM state, and frees the wlroots wrapper. The guest's EGL client extension list does **not** contain EGL_KHR_display_reference, so this conditional skips eglTerminate. The minimal wlroots control uses the public renderer lifecycle correctly; Seyrix's shutdown destroys the renderer before backend and display. Their equal LSan results confirm Seyrix is not retaining an extra EGL object.

Direct Mesa control calls eglTerminate successfully after destroying its GLES context. It has zero LSan findings at one and five cycles. That is evidence that Mesa's displayed allocations are released by display termination in this setup. The minimal wlroots and full compositor cases do not call eglTerminate because of wlroots' extension-gated policy. Thus:

- **Mesa owns the allocated objects and their EGL-internal destruction.**
- **wlroots controls the EGLDisplay lifetime for renderer-created EGL state and skips the terminating call in this environment.**
- **Seyrix correctly owns and destroys the wlroots renderer and has no demonstrated EGL cleanup defect.**

The safest repository action is no Seyrix production change. A targeted upstream wlroots discussion should ask whether EGL display termination can be made safe for renderer-owned displays when EGL_KHR_display_reference is absent, given the demonstrated per-renderer-cycle accumulation. Whether the current guard is required for shared display semantics remains an open compatibility question.

## Build, runtime, and exact commands

The controls were built in Arch WSL using GCC 16.2.1+r23+gd564253eb6c8-1, with debug symbols, frame pointers, ASan, and UBSan:

~~~sh
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O1 -fno-omit-frame-pointer \
  -fsanitize=address,undefined $(pkg-config --cflags libdrm) \
  tests/egl-gbm-lifecycle-control.c \
  -o /root/.cache/seyrix-linux/phase2c2-egl-investigation-20261026/egl-gbm-lifecycle-control \
  -lEGL -lGLESv2 -lgbm $(pkg-config --libs libdrm)

gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O1 -fno-omit-frame-pointer \
  -fsanitize=address,undefined -DWLR_USE_UNSTABLE \
  tests/wlroots-renderer-lifecycle-control.c \
  -o /root/.cache/seyrix-linux/phase2c2-egl-investigation-20261026/wlroots-renderer-lifecycle-control \
  $(pkg-config --cflags --libs wlroots-0.20 wayland-server)
~~~

Guest versions: kernel 7.2.11-arch1-1; Mesa 1:26.2.5-1; wlroots0.20 0.20.2-1; libasan and gcc-libs 16.2.1+r23+gd564253eb6c8-1. The lifecycle QEMU run used QEMU 11.1.2-1, OVMF edk2-ovmf 202608-1, q35, TCG single-thread mode, two vCPUs, 4 GiB RAM, and virtio-vga. The sanitizer ISO SHA-256 was edea044f4bbbeb1f2f9cb2068ba062fbb141ece55548cc74f926152ad763ba11.

The guest runtime environment was XDG_RUNTIME_DIR=/run/user/1000, LIBSEAT_BACKEND=logind, LIBGL_ALWAYS_SOFTWARE=1, WLR_RENDERER_ALLOW_SOFTWARE=1, and XDG_SESSION_TYPE=wayland. The guest user was seyrix (uid 1000). Control runs used ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:symbolize=1 and UBSAN_OPTIONS=halt_on_error=1. The compositor used ASAN_OPTIONS=detect_leaks=1 and UBSAN_OPTIONS=halt_on_error=1. No sanitizer findings were suppressed.

The guest helper ran these controls after the compositor was gracefully stopped:

~~~sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:symbolize=1 UBSAN_OPTIONS=halt_on_error=1 SEYRIX_DRM_PRIMARY=/dev/dri/card1 /tmp/seyrix-control-bin/egl-gbm-lifecycle-control 1
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:symbolize=1 UBSAN_OPTIONS=halt_on_error=1 SEYRIX_DRM_PRIMARY=/dev/dri/card1 /tmp/seyrix-control-bin/egl-gbm-lifecycle-control 5
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:symbolize=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seyrix-control-bin/wlroots-renderer-lifecycle-control 1
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:symbolize=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seyrix-control-bin/wlroots-renderer-lifecycle-control 5
~~~

The [guest helper](../tests/qemu-egl-lifecycle-guest.sh) also recorded each command's exit code and complete stdout/stderr in the evidence directory.

Lifecycle-only command shape:

~~~sh
SEYRIX_QEMU_WORKFLOW=phase2c \
SEYRIX_QEMU_SANITIZERS=1 \
SEYRIX_QEMU_LIFECYCLE_ONLY=1 \
SEYRIX_QEMU_GRAPHICS_DIR=/root/.cache/seyrix-linux/phase2c2-egl-investigation-20261026/qemu-lifecycle-run3 \
bash tests/qemu-graphics.sh \
  /root/.cache/seyrix-linux/phase2c2-asan-20261026/out/seyrix-phase2c-2026.10.26-x86_64.iso
~~~

The actual QEMU command, run metadata, serial log, test log, stage log, environment, raw controls, and process exit files are preserved in [egl-lifecycle evidence](evidence/phase2c2/egl-lifecycle/). The lifecycle-only guest run shut down cleanly and QEMU returned 0. Its final record/flush Expect stage was marked FAIL because the failure pattern matched the command's own LIFECYCLE_EXIT_CODES_END line, printed before unmount. The serial log then records LIFECYCLE_CONTROL_DISK=unmounted, and all status files are present. I corrected the harness to use a distinct unmount-failure marker and to check that all status files exist before reporting success. That marker fix was syntax-checked but not rerun in QEMU; it does not change the captured sanitizer outputs or process codes.

The original failed helper's command and run metadata are retained alongside the new run. The direct helper and minimal wlroots control source are [egl-gbm-lifecycle-control.c](../tests/egl-gbm-lifecycle-control.c) and [wlroots-renderer-lifecycle-control.c](../tests/wlroots-renderer-lifecycle-control.c).

## Scope and remaining uncertainty

This investigation ran the direct EGL/GBM control, minimal wlroots renderer control, and one sanitized Seyrix compositor lifecycle sequentially in QEMU. It did not rerun the 20-boot campaign or full functional regression; those passed previously and were intentionally left untouched. No Phase 2D work was started. No production code or project-wide sanitizer settings were changed. The diagnostic helper explicitly uses halt_on_error=1 so LSan findings produce nonzero process statuses; it adds no suppressions. Existing user changes were preserved.

Remaining questions are upstream-facing: whether wlroots intentionally retains EGL displays for process lifetime without EGL_KHR_display_reference, whether the same policy is required for other EGL clients sharing a display, and whether upstream would accept a narrow termination/ownership change. The original control's false eglInitialize result also remains underdetermined to one setup difference because the old and repaired environments differ in several ways; its exact call result/error and the successful corrected path are preserved above.

