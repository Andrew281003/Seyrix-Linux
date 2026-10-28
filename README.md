Seyrix-Linux


Seyrix is an independently developed, Arch Linux-based operating system focused on performance, customization, reliability, and a modern desktop experience.


The project combines the Linux kernel and established open-source infrastructure with a custom-built graphical environment and native system components.


Seyrix aims to give users greater control over how their operating system looks, behaves, and manages applications.


Vision


Our goal is to create a complete, polished operating system that feels cohesive, responsive, and customizable without sacrificing stability.


Seyrix is designed around several core principles:




Performance: Native components, efficient resource usage, and minimal unnecessary background services.


Customization: Flexible desktop layouts, appearance settings, and configurable workflows.


Reliability: Careful testing, predictable behavior, and stable system foundations.


Simplicity: A clean installation with essential applications rather than unnecessary bundled software.


Freedom: User control over applications, system configuration, and desktop behavior.




Desktop Environment


Seyrix is developing its own native graphical desktop environment using C17, Wayland, and wlroots.


Rather than maintaining separate desktop environments, Seyrix provides one unified shell with configurable behavior.


Window Management


The compositor supports three window-management modes:




Floating: Traditional freely positioned and resizable windows.


Automatic Tiling: Windows are arranged automatically using a split-tree layout.


Hybrid: Combines tiled and floating windows within the same desktop.




Additional functionality includes multiple workspaces, keyboard-driven window management, fullscreen windows, decorations, and application focus controls.


Native Desktop Shell


The desktop shell includes:




Configurable dock and taskbar layouts.


Application launcher with desktop-entry integration.


Window switcher with recent-window ordering.


Workspace navigation.


Desktop background and wallpaper support.


Desktop items and a built-in manual viewer.


Integration with the compositor through a private IPC protocol.




The desktop shell is developed as a native, unprivileged Wayland client.


Architecture


Seyrix is built on established Linux technologies while developing its own desktop experience.




Component
Technology




Operating system base
Arch Linux


Kernel
Linux


Primary language
C17


Display protocol
Wayland


Compositor framework
wlroots 0.20


Build system
Meson / Ninja


System initialization
systemd


Boot environment
UEFI


Distribution image
Archiso




The project emphasizes modularity, clear component boundaries, and independently testable subsystems.


Development Status


Seyrix is under active development.


Completed milestones




Phase 1: Bootable Arch Linux-based system image and foundational services.


Phase 2A: Reference Wayland desktop and recovery fallback.


Phase 2B: Native C17 Wayland compositor.


Phase 2C: Desktop foundations, workspaces, decorations, and compositor stabilization.


Phase 2D.1: Hybrid window management.


Phase 2D.2: Native desktop shell and graphical QEMU acceptance.




Phase 2D.2 has passed its reported graphical QEMU acceptance campaign, including desktop interaction, application launching, window switching, crash recovery, and repeated boot testing.


Development is ongoing. The project is not yet a production-ready daily-use operating system. Hardware compatibility, system applications, installation workflows, and further stability work remain.


Upcoming development




Native System Settings.


Integrated terminal and system-management commands.


Desktop integration and configuration.


Improved application compatibility.


Installer and update infrastructure.


Additional hardware and performance testing.




Testing and Reliability


Seyrix development uses automated builds, unit tests, sanitizer instrumentation, and virtual-machine testing.


The project currently uses QEMU with OVMF firmware to validate UEFI boot, graphical startup, input handling, window management, and session lifecycle behavior.


Known upstream dependency findings and incomplete features are documented rather than hidden.


Project Philosophy


Seyrix is built with a simple objective:


Create an operating system that gives users control without making them compromise on performance, stability, or design.


The project is developed incrementally, with each major subsystem tested before building more complex functionality on top of it.


Documentation


Engineering reports, architecture information, testing evidence, and development plans are maintained in the docs/ directory.


Contributing


Seyrix is an independent project under active development. Contribution guidelines and licensing details will be expanded as the project matures.



Seyrix — Built for control. Designed for freedom.


