# Seyrix-Linux Migration Audit — 2026-11-02

## Scope and disposition

This read-only audit resumed from the repository's latest available checkpoint: branch `phase2d1-hybrid-window-management`, commit `4bc271a` (`WIP checkpoint: capture EGL lifecycle work`), and the Phase 2D.1 report. No separate migration-audit checkpoint file was present. The repository's documented workspace migration feature is complete and tested; an operating-system installation or data migration is not implemented. **Do not migrate using this repository yet.**

No migration, build, package installation, disk write, cleanup, or destructive operation was performed. Existing tracked edits, untracked evidence, and repositories were preserved.

## Access checks

- PowerShell executed successfully (`Get-Date` returned November 2, 2026, 11:45 PM local time).
- `Get-Volume -DriveLetter D` succeeded. D: is fixed NTFS volume `Codex`, health `Healthy`, operational status `OK`, capacity 2,000,397,864,960 bytes, free space 2,000,227,971,072 bytes.
- `wsl --list --verbose` succeeded: `archlinux` is Running, WSL 2; `Ubuntu` is Stopped, WSL 2.
- `wsl -d archlinux -- uname -a` succeeded: `Linux DESKTOP-EQCS9VJ 6.18.54.1-microsoft-standard-WSL2 ... x86_64 GNU/Linux`.
- Arch can access the repository at `/mnt/c/Users/quake/Desktop/Coding/seyrix-linux`.
- No permission errors occurred in these access checks.

## Migration readiness findings

1. **No installation or persistent migration path exists.** The architecture and boot documentation describe an Archiso live root on EROFS with a temporary RAM overlay. They explicitly exclude partitioning, an ext4 installation, persistent storage, and an installer. The build guide also describes the image as a live development image, not an installer or production release.
2. **The roadmap leaves the prerequisite work planned.** Phase 2 calls for a supported hardware matrix and a persistent ext4 installation and recovery design before any installer is implemented. Authentication, Secure Boot, updates, recovery, and release policy also remain open decisions.
3. **The word “migration” in the latest engineering checkpoint refers to compositor windows.** Phase 2D.1 reports process-preserving movement between workspaces as passed in QEMU. The layout unit tests include workspace tree detach/reinsert checks. These results are unrelated to migrating an installed OS or user data.
4. **Current host access is adequate for further read-only work.** D: is healthy and Arch WSL is running. This confirms access only; it is not evidence of a tested Seyrix installation target or recovery plan.

## Preserved repository state

Before this report was added, Windows Git status showed branch `phase2d1-hybrid-window-management` tracking `origin/phase2d1-hybrid-window-management`, with these pre-existing changes:

- Modified: `config/archiso/airootfs/etc/localtime`
- Modified: `config/archiso/airootfs/etc/systemd/system-generators/systemd-gpt-auto-generator`
- Modified: `config/archiso/airootfs/etc/systemd/system/getty.target.wants/serial-getty@ttyS0.service`
- Modified: `config/archiso/airootfs/etc/systemd/system/multi-user.target.wants/systemd-networkd.service`
- Modified: `config/archiso/airootfs/etc/systemd/system/multi-user.target.wants/systemd-resolved.service`
- Modified: `config/archiso/airootfs/etc/systemd/system/network-online.target.wants/systemd-networkd-wait-online.service`
- Modified: `config/archiso/airootfs/etc/systemd/system/sockets.target.wants/systemd-networkd.socket`
- Untracked: `docs/evidence/phase2c2/egl-lifecycle/`

Arch-side `git status` reported the untracked EGL evidence directory but did not report the seven Windows-side modified paths. Windows `git diff` and `rg` also could not read those paths (`Function not implemented` / OS error 1920). Their contents were not inspected or changed. This cross-environment status discrepancy remains unresolved to avoid disturbing user data.

## Checkpoint and known issue

The latest committed checkpoint is `4bc271a`; the preceding substantive milestone is `d1873c6` (`feat(compositor): add hybrid window management`). The Phase 2D.1 report records the compositor workspace migration behavior as passing. The carried Phase 2C.2 wlroots/Mesa EGL lifecycle issue remains open, with Phase 2C.2 explicitly incomplete. No new tests or builds were run for this audit.

## Recommendation

Keep the project in its live-image development state until Phase 2's hardware support, persistent storage, recovery, authentication, update, and Secure Boot decisions are designed and reviewed. Any future migration plan should first identify the exact source data and destination, use a disposable or backed-up target, and define a tested recovery path. This audit did not execute or authorize such a migration.
