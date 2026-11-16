# ISO Build Environment Review — 2026-11-09

## Recommendation

Use a dedicated Arch Linux KVM virtual machine for the ISO build environment. The restricted rootless Podman configuration examined here cannot complete the required chroot mount setup. Making it work would require resolving nested user namespace, `/proc` mount, seccomp, and SELinux constraints or granting broader mount privilege. That is not a good trade for this build, and the VM keeps those operations inside a guest kernel without granting host mount capabilities.

This is an environment recommendation only. No ISO build, acceptance test, VM creation, or host security change was performed.

## Repository scope and evidence

The requested `build/build-phase2d3b-iso.sh` does not exist in this checkout or the refs inspected. The current branch is `phase2d1-hybrid-window-management` at `4bc271a`. The closest available reference is [build/build-phase2c-iso.sh](../build/build-phase2c-iso.sh#L9), which rejects non-root execution, builds Meson projects, stages files, records package metadata, and runs `mkarchiso -w ... -o ...` as root. Its build root defaults to `$HOME/.cache/seyrix-linux/phase2c-$build_id` and can be changed with `SEYRIX_BUILD_ROOT`.

The shared Archiso profile [config/archiso/profiledef.sh](../config/archiso/profiledef.sh) selects EROFS and BIOS Syslinux plus UEFI GRUB. Archiso's EROFS path creates `airootfs.erofs` from a directory using `mkfs.erofs`; it does not need a loop device or raw block device. The actual Phase 2D.3B builder may add requirements, so this review should be revisited when that script is present.

## Requirements

### Filesystem and writable paths

The source tree and build tree need normal Linux file semantics: ownership and mode changes, symlinks, hard links, and executable files. Use a local Linux filesystem such as ext4, Btrfs, or XFS, mounted read/write and with execution enabled for the build tree. Keep the build root, package cache, working tree, ISO output, and metadata on the VM's virtual disk. The current script places its work, output, profile, staging, metadata, and Meson build directories under its build root.

For a container design, only the source tree would need a read-only bind mount; the dedicated build root and package cache would be writable. The build root should be on a private mount-propagation path. The host's current Btrfs home mount is `shared`, so any container bind mount would need private propagation (for example `rprivate`) to keep mounts created during the build from propagating back to the host.

### Mounts, namespaces, and privilege

Arch's `mkarchiso` manual documents regular-user operation through `unshare` and kernel user-namespace support. `pacstrap -N` and `arch-chroot -N` create user, mount, and PID namespaces and use subordinate UID/GID mappings.

The privileged `arch-install-scripts` chroot setup mounts `/proc`, read-only `/sys`, optional EFI variables, `devtmpfs`, `devpts`, `/dev/shm`, a private bind of `/run`, and a temporary `/tmp`. Its unshare setup self-binds the target root, mounts `/proc`, recursively binds `/sys`, links `/dev/fd*`, binds only `/dev/full`, `/dev/null`, `/dev/random`, `/dev/tty`, `/dev/urandom`, and `/dev/zero`, then creates temporary `/run` and `/tmp` mounts. Both paths use chroot; the namespace path also needs usable subordinate ID mappings.

These operations require mount authority (`CAP_SYS_ADMIN` in the user namespace that owns the mount namespace) for mount operations and chroot authority (`CAP_SYS_CHROOT`) for `chroot`. In a user namespace, `CAP_SYS_ADMIN` allows bind mounts and only a kernel-defined subset of filesystem mounts; it does not automatically enable every root-mode API mount. The observed `/proc` mount failure remains unresolved. The build must also have UID/GID mappings covering the profile's ownership values and `CAP_CHOWN` in its isolated namespace for ownership changes. `chmod` requires ownership of the files or `CAP_FOWNER`. The unshare path binds existing pseudo devices, so it does not require `CAP_MKNOD` or host device capabilities. These capabilities are namespace-scoped in a rootless container; they do not grant host-wide mount or device access. Adding `CAP_SYS_ADMIN` alone would not supply the missing subordinate-ID mapping observed in the Arch image. Whether it would change the `/proc` result under another profile was not tested. The current Seyrix wrapper requires EUID 0, so it selects the direct mount path instead of Archiso's documented regular-user path. See [`user_namespaces(7)`](https://man7.org/linux/man-pages/man7/user_namespaces.7.html) for the capability limits.

### Devices and network

No host block device, loop device, GPU, USB device, EFI variable device, or `/dev/kvm` access is required to build this profile. The chroot setup uses pseudo devices; in unshare mode it binds only the six listed `/dev` entries from the build environment. Do not pass host devices or mount the host's `/dev` into a container.

Package installation needs outbound access to Arch mirrors. A VM using QEMU user-mode networking provides guest-initiated network access without forwarding host ports into the guest. The ISO build itself does not need QEMU; the current script records QEMU/OVMF tool package versions.

### SELinux

This host is Bazzite 45 with SELinux enforcing. Podman is 6.1.3, rootless, with SELinux and seccomp enabled, cgroup v2, `crun`, and overlay storage. Podman volume suffix `:Z` relabels the mounted host tree recursively. Do not apply it to the live checkout. If a container were reconsidered, use a dedicated source copy and a separate build directory, and keep SELinux separation enabled. Do not use `label=disable`, change SELinux booleans, or mount the whole home directory.

## Restricted rootless Podman probe

The non-root nested-namespace probe used the already available Arch image with no network, `--userns=keep-id`, `--cap-drop=all`, `--security-opt=no-new-privileges`, and a temporary `/tmp`. The EUID 0 mount check used rootless Podman defaults with no added capability. No ISO build was run.

- Running as container EUID 0, matching the current Seyrix wrapper, a direct tmpfs mount failed with `permission denied` under the current rootless Podman capability set.
- In a non-root nested user/mount/PID namespace, self-bind of the chroot root, recursive `/sys` bind, binds of the six pseudo devices, and tmpfs mounts for `/run` and `/tmp` succeeded.
- The `/proc` mount required by `pacstrap` and `arch-chroot` failed with `cannot mount proc read-only`.
- The `pacstrap` subordinate-ID mapping step stopped before mounting because the Arch container's `/etc/subuid` lacked a `quake` entry. The host does have subordinate UID/GID ranges, but they were not available under that name inside the image.
- The chroot execution probe was inconclusive because the scratch root did not contain a runnable command. It is not evidence that `chroot` itself failed.

**Result:** the restricted rootless configuration tested is **FAIL** for this build's required mount setup. Other rootless container configurations are **INCOMPLETE**; no container was run with added capabilities or weakened SELinux/seccomp rules. The `/proc` denial's exact cause was not isolated.

## Proposed Arch KVM VM setup

These commands are a proposal and were not executed. They assume QEMU/KVM readiness is already verified and the current Arch installation ISO has been downloaded. The VM uses a qcow2 virtual disk, QEMU user-mode networking, and no host directory or device passthrough.

On the host, create and boot the installer VM:

```bash
VM_DIR="$HOME/VMs/seyrix-archiso-builder"
ARCH_ISO="$HOME/Downloads/archlinux-x86_64.iso"
mkdir -p "$VM_DIR"
qemu-img create -f qcow2 "$VM_DIR/arch.qcow2" 160G
qemu-system-x86_64 -machine q35,accel=kvm -cpu host -smp 6 -m 12G \
  -drive file="$VM_DIR/arch.qcow2",if=virtio,format=qcow2 \
  -cdrom "$ARCH_ISO" -boot order=d \
  -nic user,model=virtio-net-pci -display gtk -device virtio-vga
```

Inside the VM's Arch installer, run `archinstall` and select only the VM's sole virtual disk for installation, using ext4 for the root filesystem. Do not attach or select any host disk, and do not share host directories. These choices apply inside the guest installer; no host-disk formatting commands are needed. After installation, relaunch with the same disk, CPU, memory, NAT networking, and display options, omitting `-cdrom` (or using `-boot order=c`).

Inside the guest as root, install the current Archiso requirements and the packages the existing builder checks:

```bash
pacman -Syu --needed \
  base-devel git archiso arch-install-scripts awk dosfstools e2fsprogs \
  erofs-utils findutils grub gzip libarchive libisoburn mtools openssl pacman \
  sed squashfs-tools syslinux util-linux gcc meson ninja pkgconf wayland \
  wayland-protocols wlr-protocols wlroots0.20 weston wlr-randr \
  qemu-system-x86 qemu-ui-gtk qemu-hw-display-virtio-gpu \
  qemu-hw-display-virtio-vga edk2-ovmf expect
install -d -m 0700 /root/.cache/seyrix-linux
```

The package set follows the current [Archiso requirements](https://github.com/archlinux/archiso/blob/master/README.rst) and the package checks in the existing builder. Arch is rolling, so package availability can change. Keep source and build data on the guest filesystem; import the exact approved source checkpoint only when the Phase 2D.3B builder is available. No build command is included here.

### Trade-offs

The VM uses more storage and memory than a container and needs a separate Arch installation. It provides the guest kernel and filesystem context needed for package installation and chroot mounts without exposing host mount capabilities. With no shared folders, device passthrough, or forwarded ports, build scripts have access to the guest disk and guest network, not the host checkout or physical disks. QEMU's emulated-device attack surface remains, so keep the VM dedicated and avoid adding host integration for convenience.

## Status

| Area | Status | Evidence |
|---|---|---|
| Static source and profile review | **PASS** | Current builder and profile inspected; requested Phase 2D.3B builder is absent. |
| Restricted rootless Podman compatibility | **FAIL** for tested setup; alternative configurations **INCOMPLETE** | Required `/proc` mount failed and exact subordinate-ID mapping stopped before mounts. |
| ISO build | **NOT RUN** | Explicitly out of scope pending approval. |
| Acceptance tests / Phase 2D.3B.5 | **NOT RUN** | Explicitly deferred. |
| VM setup | **NOT RUN** | Commands above are proposed only. |

## References

- [Arch `mkarchiso(1)` manual](https://man.archlinux.org/man/mkarchiso.1.en)
- [Arch `pacstrap(8)` manual](https://man.archlinux.org/man/pacstrap.8.en)
- [Linux `user_namespaces(7)` manual](https://man7.org/linux/man-pages/man7/user_namespaces.7.html)
- [Arch `arch-install-scripts` mount and namespace setup](https://github.com/archlinux/arch-install-scripts/blob/master/common)
- [Archiso requirements and build instructions](https://github.com/archlinux/archiso/blob/master/README.rst)
- [Podman `run` documentation: capabilities and SELinux volume labels](https://docs.podman.io/en/latest/markdown/podman-run.1.html)
- [Podman mount propagation documentation](https://docs.podman.io/en/latest/markdown/podman-container.unit.5.html)
- [Arch `wlroots0.20` package](https://archlinux.org/packages/extra/x86_64/wlroots0.20/)
