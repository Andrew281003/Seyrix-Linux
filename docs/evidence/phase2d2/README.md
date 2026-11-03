# Phase 2D.2 cloud test evidence

These raw logs record tests that ran in the disposable Debian Bookworm shell-client build container. The Arch target compositor was not built in this environment because wlroots 0.20 was unavailable. See `commands.txt` and `cloud-capabilities.txt` for commands and limits.

`session-lock-initial-race.log` preserves an early flaky run of the first lock test implementation. The test was then changed to use a ready/release handshake and the final full Meson run passed. `shell-ui-asan-lsan.log` preserves the unsuppressed Fontconfig LeakSanitizer report; its stack frames point into `libfontconfig`, with no Seyrix-owned frame.
