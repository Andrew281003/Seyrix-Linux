# Phase 1 engineering report

**Status: COMPLETE.** The Arch Linux image built successfully and passed the
UEFI QEMU boot, login, system-check, and clean-shutdown acceptance workflow on
2026-10-25. The result demonstrates a working virtual development image; it
does not establish physical hardware compatibility or production readiness.

## Host and build environment

- Windows 10 Pro x64, build 19045.7807; AMD Ryzen 9 5900X; 64 GiB RAM.
- WSL 2, version 3.0.3.0, with kernel
  `6.18.54.1-microsoft-standard-WSL2`.
- Arch Linux WSL distribution, updated from the official `core` and `extra`
  repositories. `mkarchiso` used `systemd-nspawn` to build the image.
- Pacman package signatures remained required and trusted. The recorded
  policy is `PackageRequired`, `PackageTrustedOnly`, `DatabaseOptional`, and
  `DatabaseTrustedOnly`.
- No `/dev/kvm` or nested hardware acceleration was available to WSL, so QEMU
  used TCG software emulation.

## Foundation and package state

The project-owned profile in `config/archiso/` is based on the Archiso
`baseline` profile shipped by `archiso` 91-1. It keeps upstream GRUB and
Syslinux boot infrastructure, uses Arch's stable `linux` package, and builds
an EROFS live root with a temporary RAM overlay. It includes systemd, pacman,
networkd, resolved, wired and manual wireless networking tools, firmware, and
text login. It does not include a desktop environment.

Build and test tool versions:

| Tool | Version |
|---|---|
| archiso | 91-1 |
| GRUB | 2:2.16-1 |
| QEMU x86 | 11.1.2-1 |
| QEMU x86 firmware | 11.1.2-1 |
| OVMF (`edk2-ovmf`) | 202608-1 |
| Expect | 5.45.4-5 |

The ISO contains 162 packages, including `linux` 7.2.11.arch1-1,
`linux-firmware` 20260916-1, `systemd` 262.1-1, and pacman
7.1.0.r9.g54d9411-3. The build directory records the complete image and
builder package manifests, package license metadata, Arch repository names,
pacman configuration, mirror list, profile hashes, and build log.

## Build command and artifact

From PowerShell, with the repository available to the Arch WSL distribution:

```powershell
$repo = 'C:\path\to\Seyrix-Linux'
$repoWsl = (wsl -d archlinux -- wslpath -a $repo).Trim()
wsl -d archlinux -u root -- bash "$repoWsl/build/build-iso.sh"
```

The completed build used `SOURCE_DATE_EPOCH=1792942928` and stored its outputs
outside the checkout:

```text
WSL: /root/.cache/seyrix-linux/phase1-20261025T154208Z/
ISO: /root/.cache/seyrix-linux/phase1-20261025T154208Z/out/seyrix-2026.10.25-x86_64.iso
Windows: \\wsl.localhost\archlinux\root\.cache\seyrix-linux\phase1-20261025T154208Z\out\seyrix-2026.10.25-x86_64.iso
Size: 907,358,208 bytes (about 865.5 MiB)
SHA-256: 610a4e7b4880b44be989e584a1a7e4cf4232b9a989a42f7b48f9df9c9a297d53
```

Arch is rolling release. The script repeats the build procedure, but package
versions are not pinned to an immutable repository snapshot and byte-for-byte
reproducibility has not been established.

## UEFI QEMU test

The test ran `tests/qemu-uefi.sh` with the generated ISO. The recorded QEMU
command was:

```text
/usr/sbin/qemu-system-x86_64 -machine q35,accel=tcg -cpu qemu64 -smp 2 -m 4G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
  -drive if=pflash,format=raw,file=/root/.cache/seyrix-linux/qemu-20261025T155647Z/OVMF_VARS.fd \
  -cdrom /root/.cache/seyrix-linux/phase1-20261025T154208Z/out/seyrix-2026.10.25-x86_64.iso \
  -boot order=d,menu=off -netdev user,id=net0 \
  -device virtio-net-pci,netdev=net0 -display none -serial stdio -monitor none -no-reboot
```

The test used an OVMF code image and a private disposable copy of its writable
variables, a read-only virtual CD, and user-mode networking. It attached no
virtual hard disk and passed through no host storage.

**Result: PASS.** Expect measured 51,217 ms from QEMU start to the branded
serial login prompt. It logged in as the live root user and executed shell
checks. The final run reported:

| Check | Result |
|---|---|
| Kernel | `7.2.11-arch1-1` |
| Root filesystem | `overlay` |
| PID 1 / systemd | `systemd` / `running` |
| Interactive login | `root` on the serial console |
| Network services | `systemd-networkd` and `systemd-resolved` active |
| Emulated Ethernet | DHCP address `10.0.2.15/24` |
| Memory | 3,994,660 KiB total; 3,632,192 KiB available at measurement |
| Running services | 14 systemd services |
| Installed packages | 162 |
| Hardware enumeration | PCI enumerated; first device `00:00.0` Intel 82G33/G31/P35/P31 host bridge (`8086:29c0`) |
| Clean shutdown | `systemctl poweroff`; QEMU exited successfully |

The boot-time measurement includes OVMF, bootloader, kernel, and userspace
startup under TCG. Memory and running services were sampled after login.
These figures are a baseline for this virtual configuration, not a hardware
performance comparison.

Full evidence is retained outside Git at:

```text
/root/.cache/seyrix-linux/qemu-20261025T155647Z/
```

`qemu-command.txt` and `run-metadata.txt` record the invocation and ISO
identity; `serial.log` contains the boot and verification transcript;
`test.log` records the test output and `CLEAN_SHUTDOWN=passed`.

## Failures, warnings, and limitations

- An early test-harness run expected a four-digit PCI domain, while QEMU's
  `lspci` output used a two-digit bus address. The harness pattern was
  corrected, and the final end-to-end run passed. This was a test matcher
  defect, not a boot failure.
- Pacman emitted `.pacnew` notices for `/etc/issue` and `/etc/shadow` while
  assembling the root. The branded issue text and local login were verified
  in the running image.
- GRUB emitted an SBAT warning: the installed CSV level was 4 and the
  build-time level was 6. UEFI boot succeeded with Secure Boot disabled;
  Secure Boot validation was not performed.
- Some boots logged an EROFS `-4` read interruption while
  `systemd-vconsole-setup` was being retriggered as console devices appeared.
  The service subsequently completed and boot continued. No persistent image
  read failure was observed.
- The live root preserves Archiso's empty root password for local console
  access. It does not automatically log in and OpenSSH is absent. This policy
  is for disposable development images and needs review before any production
  use.
- The UEFI path was exercised; the included BIOS Syslinux path was not.
  Physical devices, Secure Boot, Wi-Fi, graphics acceleration, installation,
  persistent storage, and updates were not tested. QEMU hardware enumeration
  does not prove physical compatibility.
- The image has no desktop, installer, or persistent ext4 root. There is no
  project-wide license yet; the copied Archiso profile retains its
  GPL-3.0-or-later notice, and package license metadata and notices are kept
  with the build/image.

## Acceptance summary

All Phase 1 acceptance criteria passed: the build succeeded, the ISO booted
under UEFI, Linux and systemd initialized, an interactive terminal was
available, shell and system commands ran, hardware enumeration succeeded,
and the guest shut down cleanly. Phase 1 is complete within the virtual test
scope described above.
