# Phase 2D.3B.5 — GTK Reverse Navigation Rendering Checkpoint

**Phase status: INCOMPLETE.** This checkpoint records an environment and evidence access blocker; it does not establish a GTK or compositor root cause.

## Status

| Requirement | Result |
| --- | --- |
| Root cause | UNKNOWN |
| Fix | NOT IMPLEMENTED |
| Bidirectional mouse navigation | INCOMPLETE |
| Keyboard navigation | NOT TESTED |
| Rendering and presentation evidence in this investigation | NOT RUN |
| Native/offline regression | NOT RUN |
| QEMU sessions executed | 0 |
| Code changes | None |

## Findings from this checkout

- The checkout is `C:\Users\quake\Desktop\Coding\Seyrix-Linux`, a Seyrix compositor repository. Its only Phase 2D report is [phase2d1-report.md](phase2d1-report.md); no Phase 2D.3B.5 report is present here.
- Repository inspection did not find GTK Settings source or the referenced forced-Cairo/redraw workarounds.
- The requested evidence directory, `/root/.cache/seyrix-linux/qemu-graphics-phase2d3b5-resume-gtk-20261104T214000Z/`, was not present in the inspected Arch WSL distro. GTK traces, Wayland logs, compositor presentation records, and screenshots therefore could not be inspected in this run.
- The mismatch described in the task remains user-supplied prior evidence; it was not independently re-verified here. No conclusion about its origin or capture target is supported by this checkout.

## Blockers and resume point

Provide or restore the Phase 2D.3B.5 report and make the named evidence directory available in the active environment. Also identify or provide the GTK Settings source/project containing the existing renderer and redraw workarounds. Resume by inspecting the preserved artifacts first, trace both category transitions, and only then decide whether a code change and focused regression are justified. If production GTK code changes, rebuild one diagnostic ISO and run the bounded 1280×720 sequence requested by the task; do not start the four-resolution campaign.

No build, test, or QEMU session was started for this checkpoint. No existing artifacts or worktree modifications were changed.
