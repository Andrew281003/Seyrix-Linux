# Phase 2D.2 Local Stage 1 Verification Report

Date: 2026-10-27 (Europe/Budapest)

**Decision: STAGE 1 PASS — ready for graphical QEMU testing.** This decision covers native build, unit, regression, and headless verification only. Phase 2D.2 remains incomplete until Stage 2 completes graphical runtime, session lifecycle, and ISO acceptance.

## 1. Baseline and preservation

- Active worktree: `C:\Users\quake\Desktop\Coding\Seyrix-Linux-Phase2D2`.
- Branch: `phase2d2-desktop-shell`.
- Initial `HEAD`: `d73e7385d1d2a17b1ca4e5129a644f8e389ec6bf`; initial worktree was clean.
- Protected original worktree remained on `phase2d1-hybrid-window-management` at `d1873c68a29b24e285e58568551b030985ef8b98`.
- Original worktree status matched preflight: seven Archiso edits and seven Phase 2C.2 EGL investigation changes. No unexpected original-worktree changes were found.
- Two pre-existing worktree entries marked `prunable` remained untouched. See `docs/evidence/phase2d2-local-stage1/preflight-preservation-snapshot.txt`.

## 2. Environment and dependencies

Builds ran in the existing `archlinux` WSL2 distribution. Environment: x86-64, kernel `6.18.54.1-microsoft-standard-WSL2`, GCC `16.2.1`, Meson `1.12.1`, Ninja `1.13.2`, pkg-config `3.0.10`, and wayland-scanner from the Arch distribution.

Resolved target dependencies: wlroots `0.20.2`, Wayland client/server `1.26.0`, `wlr-protocols` `1.0`, `wayland-protocols` `1.49`, xkbcommon `1.13.2`, GLib `2.90.1`, Cairo `1.18.6`, Pango `1.58.2`, GDK-Pixbuf `2.44.8`, and Fontconfig `2.18.3`. Full versions are in `docs/evidence/phase2d2-local-stage1/environment.txt`.

Meson builds used separate directories outside the checkout. Release builds used `--prefix=/usr --wrap-mode=nofallback`; project defaults retained C17, warning level 2, and `werror=true`. Sanitizer and analyzer builds also kept warnings as errors.

## 3. Compilation and installation

The first compositor Meson configure failed because `desktop/compositor/meson.build` referenced nonexistent `../../shell/src/shell-config.c`. The source is under `desktop/shell`; the path is now `../shell/src/shell-config.c`.

The next build caught unused static helper `send_id` under GCC 16 and `-Werror`. The helper had no callers, so it was removed. GCC `-fanalyzer` then flagged a nullable `active_workspace()` result in `seyrix_workspace_move_focused`; the function now returns when that result is null.

The extensionless installed startup scripts were checked out with CRLF in this Windows Git worktree. Bash rejected `start-seyrix` syntax. `.gitattributes` now pins LF for both `start-seyrix` and `start-wayland`. Both scripts and the ISO build script pass `bash -n` with the Arch Bash interpreter.

After these fixes, all compositor C17 units, all shell C17 units, generated Wayland protocol code, and the protocol probe compiled successfully. `meson install` staged:

- `usr/bin/seyrix-compositor`
- `usr/bin/seyrix-shell`
- `usr/bin/seyrix-protocol-probe`
- `usr/share/seyrix/Manual.txt`

The install manifest and setup/build logs are under `docs/evidence/phase2d2-local-stage1/`. No Arch ISO was built.

## 4. Test matrix

| Check | Result | Evidence or detail |
|---|---|---|
| Compositor release build and Meson suite | **PASS** | All units compiled; geometry and window-management tests passed, 2/2. Logs: `compositor-build-final.log`, `compositor-tests-final.log`. |
| Shell release build and Meson suite | **PASS** | State, configuration, app catalog, UI rendering, and session-lock tests passed, 5/5. Logs: `shell-build.log`, `shell-tests.log`. |
| Phase 2D.1 window-management regressions | **PASS** | Existing compositor geometry and window-management tests passed, 2/2. See `compositor-meson-testlog-final.txt`. |
| Configuration validation and persistence | **PASS** | Shell configuration and compositor window-management/configuration tests passed. See the Meson test logs above. |
| Application catalog and launcher safety | **PASS** | App-catalog test passed 3/3 as UID 65534, including hidden/TryExec filtering and a filename containing spaces and `;`; no injection file was created. Log: `app-catalog-unprivileged-test.log`. |
| GCC static analyzer | **PASS** | `-fanalyzer` builds passed for compositor and shell with `werror=true`; no diagnostics remained after the null guard. Logs: `compositor-analyzer-build.log`, `shell-analyzer-build.log`. |
| Compositor ASan/UBSan unit suite | **PASS** | Both compositor tests passed, 2/2, with leak detection enabled. Log: `compositor-asan-tests-final.log`. |
| Shell ASan/UBSan suite | **FAIL** | Four tests passed. UI rendering assertions passed, then LeakSanitizer reported Fontconfig allocations; details below. Logs: `shell-asan-tests.log`, `shell-asan-meson-testlog.txt`. |
| Headless Pixman integration with ASan/UBSan compositor | **PASS** | 14 checks passed: compositor, terminal and panel readiness; state/decoration/destruction probes; two-output assignment and reconfiguration; clean shutdown; no ASan/UBSan/LSan diagnostic. Matrix: `headless-asan/results.tsv`. |
| Headless pointer watcher | **NOT RUN** | Headless backend exposed no pointer capability. See `headless-asan/pointer_watch_startup.log`. |
| Direct private shell IPC packet tests | **NOT RUN** | No test directly covers malformed/truncated `SOCK_SEQPACKET` frames or send backpressure. Both endpoints and packet structures compiled; code review covered validation. |
| Shell crash/restart runtime test | **NOT RUN** | Deferred to Stage 2 session-lifecycle acceptance. |
| Bash syntax and `git diff --check` | **PASS** | Includes both installed startup scripts, build script, lock test, headless test, and probe checker. Logs: `shell-script-syntax-final.log`, `git-diff-check-final.log`. |
| Full ISO and QEMU | **NOT RUN** | Explicitly excluded from Stage 1. |

The headless run's full result matrix is `docs/evidence/phase2d2-local-stage1/headless-asan/results.tsv`. Its temporary `/usr/bin/seyrix-shell` symlink was removed; no terminal child remained after shutdown.

The cloud evidence preserves an early session-lock race failure. The lock test then added a ready/release handshake; the final cloud suite passed, and the local suite also passed 5/5. The raw initial run remains in `docs/evidence/phase2d2/session-lock-initial-race.log`.

## 5. Integration review

- The compositor creates a private `SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK` socket pair for the shell. Send paths use `MSG_DONTWAIT | MSG_NOSIGNAL` and treat short sends or errors as channel failure.
- Receive uses `MSG_TRUNC`; handlers reject oversized, short, wrong-version, wrong-size, unknown-type, and out-of-range workspace packets. Window actions resolve IDs against mapped windows. Metadata is bounded by the protocol structure sizes.
- Socket failure closes the channel, restores/hides overlay state, and schedules bounded restart attempts. The restart budget is five cumulative attempts and does not reset after a stable run; confirm this policy during Stage 2 crash/restart testing.
- Layer-shell focus checks both compositor-owned shell PID and expected namespace. Unmap and destroy callbacks restore overlay focus state. Window removal clears saved focus before freeing the window; callbacks run on the compositor event loop.
- Workspace/window updates remain compositor-authoritative. Existing Phase 2D.1 geometry and management regressions passed. Static review found no confirmed Phase 2D.1 invariant violation.
- Shell and terminal children join the session process group. Headless sanitizer shutdown exited cleanly with no remaining terminal process. Process-group cleanup does not contain applications that deliberately call `setsid()`; this is not a strict no-orphans guarantee.

No test currently exercises private IPC malformed-packet handling, shell restart timing, or focus restoration after a shell crash. The headless integration run exercises normal shell startup, layer mapping, compositor interactions, and shutdown.

## 6. Memory-safety investigation

The cloud report recorded `114,879` bytes in `3,900` allocations during sanitized UI rendering. A second cloud suite run reported `119,945` bytes in `4,010` allocations. The Arch WSL reproduction reported `141,472` bytes in `4,817` allocations. Allocation stacks originate in `libfontconfig` (`FcValueSave` and font-set matching) through Pango text rendering. The UI test uses a missing wallpaper path, and releases its Pango layouts/font descriptions, Cairo contexts, and surfaces.

An isolated Pango/Cairo control reproduced `30,465` bytes in `916` allocations after releasing those same objects. The result was identical with and without a diagnostic `FcFini()` call. This supports Fontconfig/Pango process-global retained cache state, not a Seyrix-owned resource lifecycle leak or a test object left unreleased. The UI test still **fails LSan**; no finding was suppressed and no cleanup call was added to Seyrix.

The Phase 2C.2 wlroots/Mesa EGL teardown finding is separate. The headless run used Pixman and did not exercise EGL. Preserve the existing EGL finding and evidence for graphical Stage 2; this report does not classify it as a Seyrix leak or resolve it.

Raw sanitizer logs and the minimal control source are under `docs/evidence/phase2d2-local-stage1/`. `manifest.sha256` records checksums for all 89 payload files, excluding itself. Git attributes preserve these evidence files byte-for-byte across Windows checkouts, including the raw Linux log line endings.

## 7. Limitations and readiness

Stage 1 evidence supports starting graphical QEMU acceptance. Stage 2 must still verify visible desktop behavior, shell crash/restart and session lifecycle, graphical focus/overlay behavior, and the completed ISO. Pointer behavior is not covered by the headless backend. The private IPC test gap and the cumulative restart limit should receive attention during Stage 2.

This is **not** an overall Phase 2D.2 PASS. No ISO or QEMU run occurred.

## 8. Changed files and commits

Reviewed source fixes are in commit `ee726fd2ff1e0792a9b421c24bb7e298d6e51252` (`fix(compositor): repair Phase 2D.2 build integration`). Exact source files:

- `.gitattributes`
- `desktop/compositor/meson.build`
- `desktop/compositor/src/shell.c`
- `desktop/compositor/src/workspace.c`

This report and raw logs are committed separately on `phase2d2-desktop-shell`. The final evidence commit is reported with the Stage 1 result.

The report/evidence commit also adds the evidence-directory byte-preservation rule in `.gitattributes`; the manifest lists every evidence payload path.
