# Seyrix-Linux

Seyrix is an independently developed Linux-based operating system project focused on performance, modern hardware compatibility, customization, and a polished desktop experience.

## Vision

Seyrix aims to build a cohesive operating system experience on the Linux kernel and established open-source infrastructure. The project plans to develop its own desktop environment, system applications, installer, and supporting system experience.

Two desktop styles are envisioned:

- **Aero** — a macOS-inspired desktop experience.
- **Chroma** — a Windows-inspired desktop experience.

These are design directions; they do not imply compatibility with or affiliation to other operating systems.

## Development status

Phase 1 is complete on the `phase1-arch-foundation` branch. The project has
generated an x86-64 Arch Linux development ISO and verified a complete UEFI
boot, interactive serial login, system checks, and clean shutdown in QEMU.
This is a live development image, not an installer or production release. See
the [Phase 1 report](docs/phase1-report.md) for measurements and limitations.

The repository does not yet have a project-wide license. The included Archiso
profile retains its upstream GPL-3.0-or-later notice, and redistributed Arch
packages retain their package license files in the image.

## Documentation

- [Architecture overview](docs/architecture.md)
- [Proposed roadmap](docs/roadmap.md)
- [Phase 1 report](docs/phase1-report.md)
- [Build guide](docs/build-guide.md)
- [Boot architecture](docs/boot-architecture.md)

## Contributing

Development and contribution guidance will be added as the project establishes its technical foundations and licensing approach.
