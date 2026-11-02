# Phase 2D.3B.4d — Diagnostic Harness Regression & Controlled Verification

**Overall result: INCOMPLETE.** All offline harness regression fixtures passed. The one authorized QEMU boot reached the graphical session, but stopped before Settings launched because the temporary harness snapshot omitted `qemu-guest-helper.sh`. This run therefore did not execute or independently verify the live Settings page-selection sequence. It provides no evidence of a compositor or GTK rendering defect.

No ISO was rebuilt or promoted. This 4d pass changed only diagnostic harness and regression fixture files. Pre-existing uncommitted compositor and GTK edits in the worktree were left untouched.

## Reviewed Phase 2D.3B.4b and 4c evidence

The earlier reports, traces, logs, and screenshots were reviewed:

- [Phase 2D.3B.4b report](phase2d3b4b-settings-trace-report.md) documents a real Appearance → Desktop & Dock transition. For Settings PID 798, GTK recorded the Desktop selection and Appearance unmap after the QMP click at (320,156) and Home/Down/Enter. The compositor recorded a buffer attach/damage, scene commit, and `presented=1`; page-content ROI pixels changed by 6,850. The 4b runner remained **INCOMPLETE** because it checked environment on a different Settings PID and its serial stage marker handling was unreliable. Treat 4b as supporting historical UI evidence, not a passing 4d run.
- [Phase 2D.3B.4c report](phase2d3b4c-final-trace-verification.md) documents a traced Settings PID 776 and a complete saved client trace, but no page-selection input. The trace uses `wl_surface#16`; the old harness only accepted `wl_surface@N`, so it counted zero operations and skipped input. Its “after” image was a fallback Appearance capture, with no page-content change: the comparator found 0/140,740 page-content ROI pixels and 0/307,200 full-frame pixels changed. The corrected offline replay now parses 26 surface requests for PID 776 and client object 16. No compositor identity is inferred from that numeric client object ID.

## Offline regression results

The saved 4c trace was replayed through the same production parsing and launch-gating functions used by the guest helper. The fixtures and assertions are in [the regression script](../tests/settings-render-trace-regression.sh), [trace fixtures](../tests/fixtures/settings-render-trace/), and [pixel regression script](../tests/settings-render-pixel-regression.py). The complete latest output is preserved in [offline-regression.log](evidence/phase2d3b4d-harness-regression/offline-regression.log).

| Case | Result |
|---|---|
| Replay saved 4c trace through production parser and launch gate; use PID 776 for trace and environment checks | PASS — 26 client surface operations found |
| Parse both `wl_surface@N` and `wl_surface#N` forms | PASS |
| Empty, malformed, truncated, and unrelated-process traces | PASS — fail closed or cannot satisfy the Settings PID gate |
| Distinguish client object IDs from compositor surface fields/pointer IDs | PASS — correlation requires same PID and compositor `wl_surface=` field |
| Permit input when the optional client protocol text format is absent, while still requiring the correct traced Settings process/environment | PASS |
| Verify actual input and GTK Appearance → Desktop & Dock gates are process-scoped | PASS |
| Preserve complete client and compositor logs before simulated guest shutdown | PASS in production capture-function fixture |
| Protect the serial shell prompt from stage-result markers | PASS — the synthetic failed stage result stayed ordered before the next command’s successful prompt check |
| Pixel comparator with verified input, GTK transition, matching PID, and before/after QMP evidence | PASS — missing, skipped, failed, or mismatched evidence cannot report PASS |
| Pixel comparator with verified input but unchanged page-content pixels | PASS — reports FAIL |

The production comparator reports **INCOMPLETE** when required input, GTK, PID, or QMP evidence is missing/failed; changed fallback screenshots alone cannot pass. The regression suite also checked Bash syntax, Tcl source completeness, and `git diff --check`.

After the QEMU run exposed the incomplete snapshot, a host-side preflight was added in [qemu-graphics-preflight.sh](../tests/qemu-graphics-preflight.sh) and wired into [qemu-graphics.sh](../tests/qemu-graphics.sh). Its offline fixture confirms the trace workflow rejects a staged snapshot missing the phase workflow’s guest helper before starting QEMU. This guard was added after the executed boot and was not part of that boot’s source snapshot.

## Authorized QEMU run

- ISO: `/var/tmp/seyrix-linux/phase2d3b4a-native-20261031T141141Z/out/seyrix-phase2d3b-2026.10.31-x86_64.iso`
- SHA-256: `7bf470ba904004ddb6e1670d5c162690a2df0e4cd9b16cabecada1b02c91c196`; size: 1,151,631,360 bytes.
- The ISO hash matched before/during the run. QEMU used the ISO read-only; no rebuild or promotion occurred.
- One QEMU VM boot ran under QEMU run ID `20261031T170813Z`, from 17:08:13Z to 17:10:25Z. The QEMU process exited. No retry was started.
- Preserved evidence: [full test log](evidence/phase2d3b4d-harness-regression/qemu-run/test.log), [serial log](evidence/phase2d3b4d-harness-regression/qemu-run/serial.log), [stage results](evidence/phase2d3b4d-harness-regression/qemu-run/stage-results.log), [run metadata](evidence/phase2d3b4d-harness-regression/qemu-run/run-metadata.txt), [QEMU command](evidence/phase2d3b4d-harness-regression/qemu-run/qemu-command.txt), and [source hashes for the staged harness](evidence/phase2d3b4d-harness-regression/harness-source-hashes.sha256). The copied serial log hash matches its run-directory source.

### Boot and failure

Serial login succeeded directly. `TTY1_GETTY_ACTIVE=active` and `TTY1_DEV_SESSION_READY=passed`; no tty2 recovery was needed. The compositor reached `COMPOSITOR_READY=passed`, started on `WAYLAND_DISPLAY=wayland-0`, activated a 640×480 output, and mapped the terminal. Serial output also records the Settings read-only endpoint becoming ready at `/run/user/1000/seyrix/settings-v1.sock` (protocol 1.0, revision 1).

The Expect harness then failed in `install_guest_helper`: the temporary staged directory lacked `harness/qemu-guest-helper.sh`. The file exists in the worktree; it was omitted when this run’s ext4 harness snapshot was assembled. The error is preserved in `test.log` near its end. The run result is **INCOMPLETE** with exit status 1. The screenshot-comparison stage correctly reported `INCOMPLETE reason=missing-qmp-screenshots`.

### Settings, trace, presentation, and screenshots

Because staging failed before the Settings helper installation:

- Settings was not launched. There is no 4d Settings PID, `/proc` environment check, initial GTK Appearance observation, input delivery, or GTK page-selection result.
- The 4d client Wayland trace and per-Settings compositor presentation events were not captured. The extracted `settings-render.trace` and `seyrix-compositor.log` files are empty; raw compositor startup and endpoint-readiness events remain in `test.log` and `serial.log`.
- No QMP Settings “before” or post-selection screenshot was taken. The retained PPMs are tty/setup captures, not Settings page images. The 4d pixel comparison is **INCOMPLETE**, not a page-content PASS or FAIL.
- The compositor endpoint service was ready, but whether Settings connected to it or received live state is **NOT OBSERVED**. Endpoint readiness alone does not prove a client connection.
- The Appearance → Desktop & Dock transition was **NOT OBSERVED in 4d**. The visible transition and content-pixel change from 4b remain historical supporting evidence only. In 4c, the displayed state was last-known/disconnected.

The raw compositor log in the run shows compositor/session startup and terminal presentation activity only; it contains no Settings client update. Offline capture fixtures prove the helper’s pre-shutdown preservation behavior, but this incomplete run could not exercise that capture path with a live Settings trace.

## Storage and preservation

After the run, `/var/tmp/seyrix-linux` measured 32,934,047,644 bytes (about 30.67 GiB). Including the 4 GiB run reserve, this is about 34.67 GiB against the 40 GiB cap. WSL reported 964,410,672 KiB available, well above the 30 GiB free-space minimum. Existing worktrees, uncommitted development changes, verified ISOs, WSL VHDX, and backup were preserved; no cleanup, reset, merge, or push was performed.

## Acceptance still required

The offline harness regression objective is **PASS**, but the complete phase objective is **INCOMPLETE**. The single QEMU boot authorized for this phase has been used. The corrected staged-support preflight now detects this specific omission offline; a live retry requires fresh authorization. No ISO rebuild is needed or authorized.

A future authorized run must reach Settings through the installed diagnostic wrapper, verify the exact Settings PID and environment, show initial Appearance, deliver the category-selection input, confirm the GTK Desktop & Dock state, capture the matching client Wayland and compositor surface/scene/presentation events before shutdown, and compare genuine before/after QMP page-content pixels. It must also observe whether Settings connects to the read-only endpoint and receives live compositor state. The other Phase 2D.3B acceptance items remain as listed in the 4c report.


