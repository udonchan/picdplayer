# PiCDPlayer Agent Guide

This document defines the development, build, deployment, and runtime
debugging conventions for PiCDPlayer.

Agents working on this repository should follow these conventions unless
the user explicitly requests otherwise.

---

## Project overview

PiCDPlayer is an appliance-style physical audio CD player built around a
Raspberry Pi.

The main runtime components are:

- `cdplayerd`: C++20 CD player daemon
- Cage: minimal Wayland compositor
- Chromium: kiosk UI runtime
- systemd: service management
- Linux DRM/KMS / vc4: HDMI graphics
- ALSA: audio output
- Linux CEC API: TV remote / HDMI-CEC integration

The primary target currently is a Raspberry Pi 3 running 64-bit
Raspberry Pi OS based on Debian Trixie.

The target architecture is:

    Linux/aarch64

The Raspberry Pi is primarily a runtime and hardware integration target,
not the primary development/build machine.

---

## Development architecture

The normal development flow is:

    edit on Mac
        ↓
    build in Linux/aarch64 Docker environment
        ↓
    CMake install into local staging tree
        ↓
    deploy staging tree to Raspberry Pi
        ↓
    test/debug on Raspberry Pi

Do not replace this workflow with direct development and compilation on
the Raspberry Pi unless explicitly requested.

The development Mac is Apple Silicon, so the Debian arm64 Docker image
runs as a native aarch64 environment.

---

## Build environment

Linux/aarch64 production builds must use the repository Docker build
environment.

The Dockerfile is located at:

    ./Dockerfile

The standard image name is:

    picdplayer-build

Build the image with:

    docker build -t picdplayer-build .

The Docker image contains build dependencies.

Do not rely on packages or modifications installed interactively inside
temporary containers. If a dependency is required for a normal build,
add it to the Dockerfile.

The source working tree is bind-mounted into the container at:

    /src

Build artifacts therefore remain in the host working tree even though
the compiler runs inside Docker.

---

## Build configuration

The production-equivalent CMake configuration currently enables:

    CMAKE_BUILD_TYPE=Release
    ENABLE_METADATA=ON
    ENABLE_API=ON
    INSTALL_SYSTEMD_UNIT=ON
    INSTALL_SYSTEMD_KIOSK_UNIT=ON
    CMAKE_INSTALL_PREFIX=/usr/local
    PICDPLAYER_SERVICE_USER=picdplayer

The build directory is:

    build-container/

Do not use the ordinary macOS compiler to produce the Raspberry Pi
runtime binary.

Do not assume that a successful macOS-native build is equivalent to the
Linux/aarch64 target build.

When `scripts/build-container.sh` exists and supports the required build,
prefer it over manually reconstructing Docker and CMake commands.

Re-running CMake configuration before an incremental build is acceptable
and preferred when CMake configuration may have changed.

---

## Installation staging

CMake is the authoritative source for deciding which files belong to an
installed PiCDPlayer system and where those files are installed.

Do not duplicate the CMake install manifest manually in deployment
scripts.

Installation must first be performed into a DESTDIR staging tree.

The host-side staging directory is:

    stage/

Conceptually:

    cmake --install build-container
        ↓ DESTDIR
    stage/
        └── usr/local/...

The staging tree currently contains items such as:

    /usr/local/bin/cdplayerd
    /usr/local/libexec/picdplayer-kiosk
    /usr/local/lib/systemd/system/picdplayer.service
    /usr/local/lib/systemd/system/picdplayer-kiosk.service
    /usr/local/share/picdplayer/ui/default/...

When new installed files are introduced, prefer adding appropriate
`install()` rules to CMake rather than adding special-case copy commands
to deployment scripts.

---

## Raspberry Pi deployment target

The Raspberry Pi is referenced through the SSH host alias:

    picdplayer-pi

Do not hard-code the Raspberry Pi IP address, username, SSH key path, or
other developer-specific SSH settings in this repository.

Machine-specific configuration belongs in the developer's SSH
configuration, normally:

    ~/.ssh/config

Repository scripts should use:

    ssh picdplayer-pi

rather than an IP address.

Deployment scripts may allow the target alias to be overridden using:

    PICDPLAYER_TARGET

For example:

    PICDPLAYER_TARGET=picdplayer-test ./scripts/deploy.sh

---

## Deployment

The standard deployment script is:

    ./scripts/deploy.sh

It runs on the development Mac.

The expected deployment flow is:

    Mac stage/
        ↓ rsync
    Pi ~/stage/
        ↓
    stop kiosk
        ↓
    stop daemon
        ↓
    install staged filesystem tree
        ↓
    systemctl daemon-reload
        ↓
    start daemon
        ↓
    start kiosk
        ↓
    verify services

The Raspberry Pi staging directory is:

    ~/stage/

The staged tree mirrors the target filesystem root.

For example:

    ~/stage/usr/local/bin/cdplayerd

is installed as:

    /usr/local/bin/cdplayerd

Deployment currently uses rsync to apply the staging tree to `/`.

Never use `rsync --delete` when synchronizing the staging tree to `/`.

The staging tree contains only PiCDPlayer-managed files. It is not a
complete representation of the Raspberry Pi root filesystem.

Deleting files from `/` based on the staging tree would therefore be
dangerous.

The current deployment mechanism does not automatically remove obsolete
files that were present in an older PiCDPlayer installation but were
later removed from the CMake install rules.

If obsolete installed files become a practical problem, introduce an
explicit install manifest/uninstall mechanism or package PiCDPlayer
(e.g. as a Debian package) rather than using broad filesystem deletion.

---

## systemd services

The primary services are:

    picdplayer.service
    picdplayer-kiosk.service

The daemon should normally be started before the kiosk.

When replacing installed binaries or runtime files during deployment,
stop the kiosk first and then stop the daemon.

After installing systemd unit files, run:

    sudo systemctl daemon-reload

Then start:

    picdplayer.service
    picdplayer-kiosk.service

Do not assume that copying a new unit file automatically updates
systemd's loaded unit definition.

---

## Runtime debugging

Runtime and hardware integration testing must be performed on the
Raspberry Pi.

Important target-specific functionality includes:

- optical drive access
- CD-DA reading
- ALSA HDMI audio
- HDMI-CEC
- DRM/KMS graphics
- Cage
- Chromium kiosk behavior
- systemd startup ordering

Do not assume that Docker can meaningfully validate these hardware
integration paths.

Docker is primarily the reproducible Linux/aarch64 build environment.

---

## Chromium debugging

The kiosk Chromium instance may expose the Chrome DevTools Protocol on
the Raspberry Pi for development.

Remote debugging should normally be accessed from the Mac, for example
through SSH port forwarding.

Avoid running heavyweight development tools directly on the Raspberry
Pi while investigating runtime performance.

In particular, the Raspberry Pi 3 has limited CPU and memory resources,
and Chromium itself can consume a substantial fraction of them.

Profiling and debugging tools can perturb performance measurements.

---

## Performance work

When investigating startup or UI performance, distinguish between:

- system boot completion
- first display output
- PiCDPlayer daemon readiness
- API readiness
- Chromium startup
- DOM load
- WebSocket connection
- first UI render
- CD playback readiness

Do not use `systemd-analyze time` alone as a measurement of perceived
PiCDPlayer startup time.

When changing UI update behavior, consider CPU, layout, paint, and GPU
cost on the Raspberry Pi 3.

Avoid unnecessary high-frequency DOM updates.

---

## Source of truth

Keep responsibilities separated:

    Dockerfile
        build environment and dependencies

    CMake
        compilation and install layout

    scripts/build-container.sh
        reproducible build/staging procedure

    scripts/deploy.sh
        transfer and target installation procedure

    systemd units
        runtime service lifecycle

    AGENTS.md
        development conventions for agents

Do not copy the same installation file list into multiple layers.

When behavior changes, modify the layer that owns that behavior.

---

## Safety rules for agents

Before performing deployment or other operations that modify the
Raspberry Pi:

1. Build using the documented Docker/aarch64 workflow.
2. Stage installation through CMake DESTDIR.
3. Use the configured SSH alias rather than hard-coded network details.
4. Stop runtime services before replacing their binaries.
5. Never use `rsync --delete` against the Raspberry Pi root filesystem.
6. Do not modify unrelated Raspberry Pi system configuration unless the
   task explicitly requires it.
7. Do not install development dependencies on the Raspberry Pi merely to
   make a build succeed.
8. Preserve the distinction between build-time problems and
   target-runtime/hardware problems.
9. Verify service state and relevant logs after deployment.

If a deployment step would require broader privileges or destructive
filesystem operations than described here, stop and inspect the
situation rather than improvising.

---

## Preferred workflow

For normal implementation work, use:

    edit
      ↓
    ./scripts/build-container.sh
      ↓
    ./scripts/deploy.sh
      ↓
    runtime verification on picdplayer-pi

The goal is to keep the Raspberry Pi close to the actual appliance
runtime environment while keeping compilation and development workload
on the Mac.
