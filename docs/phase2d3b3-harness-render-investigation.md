# Phase 2D.3B.3 Harness and rendering investigation

Date: 2026-10-31

## Scope and preservation

Active checkout: `C:\Users\quake\Desktop\Coding\Seyrix-Linux-Phase2D2`, branch `phase2d3-system-settings`.

Protected checkout: `C:\Users\quake\Desktop\Coding\Seyrix-Linux`. It was not edited. Existing uncommitted work in both checkouts remains untouched. No reset, clean, checkout, build, ISO promotion, push, or merge occurred. The verified Phase 2D.2 ISO remains at `/var/tmp/seyrix-linux/verified/seyrix-phase2d2-2026.10.27-x86_64.iso`, SHA-256 `9ace6c35c5c67f7bb2e4a8b181fe04d587c9cdda7b49df937abe52f190a49fb2`. WSL distribution remained registered; VHDX and backup were not changed.

Storage at inspection: `/var/tmp/seyrix-linux` used 29,462,765,568 bytes, below 40 GiB. `/dev/sdd` had 993,108,484,096 bytes free, above 30 GiB.

## 1. Harness defects and corrections

The prior graphics wrapper marked PASS when its own `expect` command exited zero. It did not require every `STAGE_BEGIN` to have a PASS result, reject an earlier failure marker, or record QEMU exit independently. The UEFI wrapper also converted every failed Expect exit to status 1 and previously lacked stage completeness checks.

`tests/qemu-graphics.sh` and `tests/qemu-uefi.sh` now require at least one stage, equal stage-begin and stage-pass counts, no FAIL/INCOMPLETE stage, no `.seyrix-failed` marker, zero recorded Expect exit, and observed QEMU termination. Final result writes through a temporary file and atomic rename. Any mismatch is INCOMPLETE. Retention receives success only when final validation passes. QEMU exit is recorded as unavailable if the run-owned process remains alive.

The graphics Expect helpers now restore their configured stage timeout after waiting for the control prompt. Previously `run_serial_capture` and `run_serial_check` restored an 8-second prompt timeout, allowing later input or shell output to escape the intended stage budget.

`tests/qemu-status-test.sh` fixture suite: **PASS, 7/7**. Cases cover all stages passing, missing stage result, explicit timeout, failed stage, external timeout exit 124, nonzero QEMU exit, and stale failure marker. Bash syntax checks pass. Fixture suite uses temporary local directories and removes them.

## 2. Serial-getty recovery

Existing rule: `SUBSYSTEM=="tty", KERNEL=="ttyS0", TAG+="systemd", ENV{SYSTEMD_WANTS}+="serial-getty@ttyS0.service"`. Rule asks systemd to start the existing template service when udev adds ttyS0. This is a plausible late activation request. `SYSTEMD_WANTS` acts on device activation and may not restart an already active device unit merely because the rule was reloaded. A diagnostic experiment must trigger a new device event or run in a fresh guest with rule installed before device activation.

The existing service binds and orders against `dev-ttyS0.device`; prior evidence shows its initial 90-second wait timed out, then ttyS0 appeared around 159–160 seconds and getty remained inactive. Guest control uses a separate named virtio serial port. No diagnostic-only rule injection or device trigger ran in this task. No new kernel, udev, systemd, getty, or login-prompt evidence was collected.

Status: **INCOMPLETE**. Rule remains unverified. No claim that UART initialization delay is fixed.

## 3. Graphical session readiness

The previous compositor check used the full executable path with `pgrep -f`, which avoids Linux `comm` truncation, but readiness accepted any `wayland-*` socket and only checked for a terminal process. It did not verify that the socket belonged to the running Seyrix session or that a Wayland client could connect.

The readiness command was strengthened to require the uid-1000 compositor executable, expected `/run/user/1000` environment, live process state, mapped Seyrix panel, shell process, socket under the runtime directory, and successful bounded `wayland-info` connection. It prints compositor PID, uid, executable, state, socket owner/mode, runtime path, and shell PID, plus compositor and session logs on failure.

This new readiness check was not exercised against a guest in this task. A syntactically valid expect script and live guest session were not demonstrated. The prior report documents one earlier candidate run where compositor, Settings launch, and Appearance capture succeeded; later targeted trace attempts did not reach page selection.

Status: **INCOMPLETE** for new readiness proof.

## 4. Single-frame Settings diagnosis

No new Settings run occurred. Existing Phase 2D.3B report records direct QMP evidence: after clicking Desktop & Dock, GTK selected the expected page; Desktop page was mapped at 583×393 while Appearance was unmapped; screenshot content did not change. The client-side Wayland trace attempts failed before reaching the click. Therefore no correlation exists for GTK frame scheduling, client buffer attachment, `damage_buffer`, surface commit, wlroots commit/damage, scene output commit, or QMP pixels in one run.

Earliest confirmed boundary is only bounded by the existing observations: GTK selection/mapping succeeded, but changed page pixels did not reach the QMP framebuffer. Exact failing boundary is **unresolved**. Minimal production correction cannot be responsibly recommended from current evidence. Do not add redraw calls.

## 5. VM budget and blocking condition

VM boots used in this task: **0 of 2**. No unrestricted campaign ran. Current Arch WSL filesystem contains the retained verified Phase 2D.2 ISO and the existing failed Phase 2D.3B ISO. The existing ttyS0 udev rule is only in the active source overlay, not in either ISO. Testing it would require a rebuild, which this task forbids without separate authorization. No diagnostic-only rule injection path was established before running a VM. Therefore booting either image could not evaluate the rule; remaining source-only rendering readiness changes are also unverified against the actual session.

Stopping condition reached: no targeted boot can test the new rule without packaging it, and no targeted trace can be claimed without first proving graphical readiness. No additional VM or build is justified within this scope.

## 6. Objective disposition

| Objective | Status | Evidence |
|---|---|---|
| Harness rejects false PASS and propagates nonzero exits | **PASS** | Wrapper validation logic; fixture suite 7/7 |
| Graphical readiness mechanism checks process, session socket, and connectivity | **INCOMPLETE** | Source strengthened; guest execution not performed |
| Serial-getty recovery evaluated with live evidence | **INCOMPLETE** | Existing rule inspected; no runtime injection or fresh ISO test |
| Real graphical session reachable under this harness | **INCOMPLETE** | Historical candidate evidence exists; updated check not run |
| Actual Settings transition traced to earliest rendering boundary | **INCOMPLETE** | Historical GTK selection/mapped state and stale QMP pixels; no boundary trace |
| Storage safeguards and original-worktree preservation | **PASS** | 29.46 GB artifact use; 993.1 GB free; original checkout untouched |

No Phase 2D.3B completion claim. No Phase 2D.3C work. No ISO promoted.

## 7. Git status

Branch remains `phase2d3-system-settings`. Pre-existing modified and untracked files remain; no commit was created. This task adds `tests/qemu-status-test.sh` and this report, and edits the two QEMU wrappers plus the graphics Expect script. `git diff --check` reported only existing line-ending notices and no whitespace errors.
