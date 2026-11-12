---
name: sandbox
description: Checking that the workspace's program builds, passes its tests and runs on other operating systems and processors - Linux distributions on amd64, arm64, riscv64 and more - in Docker sandboxes with sandbox_* tools; choosing systems from the catalog, creating sandboxes, validating across all of them, reading failures and fixing them on the host.
keywords: [sandbox, docker, linux, arm64, aarch64, riscv64, cross-platform, portability, distribution, alpine, musl, emulation, qemu, container, validate]
triggers:
  - "does it work on linux"
  - "test on linux"
  - "make it portable"
  - "on arm"
  - "other operating systems"
  - "in a container"
  - "in docker"
  - "validate on"
  - "big endian"
---

# Sandboxes

A sandbox is a Linux machine in Docker that this workspace's code runs in: an operating system image, a processor
(native, or another one under QEMU emulation), the packages it needs. The workspace folder is mounted at `/workspace`
(read-write, except `.ied/`), so a sandbox builds the same files you edit here - no copying, no pushing. Nothing a
sandbox runs touches this machine. Definitions are files, `.ied/sandbox/<name>/sandbox.json`, committed with the
workspace.

## Workflow

1. `sandbox_list` - the engine and the sandboxes there are. If Docker is not `ready`, say so; `sandbox_engine(action:
   "start")` starts Docker Desktop (it asks the owner). If Docker is not installed, tell the owner how (the result says).
2. No sandbox for what you need: `sandbox_catalog` (with a query - `"arm64"`, `"debian"` - for details) and
   `sandbox_create`. Choose what the question needs, not everything:
   - "Does it work on Linux?" - one mainstream glibc system: `ubuntu:24.04` or `debian:13`.
   - Older or newer toolchains: the oldest LTS the project supports (`ubuntu:22.04`) and a rolling one (`arch`, newest
     compilers find new warnings and errors).
   - musl / BusyBox (`qsort_r`, `<execinfo.h>`, locale, `ls --color` in scripts): `alpine`.
   - RPM world: `fedora` or `rocky`.
   - Another processor: the same system with `platform` - `linux/arm64` (alignment, char signedness, weak memory
     ordering), `linux/arm/v7` or `linux/386` (32-bit: pointer size, `long`, time_t), `linux/s390x` (big-endian),
     `linux/riscv64`. Emulated runs are 5-20x slower: give them a generous `timeout_s`.
   Name it for what it is (`ubuntu`, `ubuntu-arm64`, `alpine`). Give `presets` (`build`, `cmake`, `ninja`, `python`, ...)
   and a `validate` command. The call shows the owner the file; they approve it.
3. `sandbox_validate` - builds each image the first time (minutes; once), then runs the command everywhere and answers
   a pass/fail matrix with each failure's last lines.
4. A failure: read it like a build error here. `/workspace/src/a.c:41` is `src/a.c` line 41 in the workspace. Fix the
   source on the host (`fs_edit`), never by editing inside the sandbox. Re-run that sandbox (`sandbox_run` with the same
   command, or `sandbox_validate(names: [...])`), then validate everywhere again - including this machine's build
   (`build_run`) - before saying it works.
5. Report the matrix: which systems and processors pass, what failed and why, what you changed.

## Rules that save time

- **Build into a folder of the sandbox's own**: every run has `IED_SANDBOX` (the sandbox's name) and `IED_PLATFORM`
  (`linux/arm64`), so one command serves every sandbox - `cmake -B build/$IED_SANDBOX`, `cargo build --target-dir
  target/$IED_SANDBOX`. Never the host's build folder, and never one folder for all: the workspace is shared with this
  machine and between sandboxes, and a validation runs several at once. Build systems that name the platform in their
  output folder (Nuke) need nothing.
- **Network is off by default** at run time (`network: "none"`); installing packages always has it. A build that
  downloads dependencies (cargo, npm, pip, go modules, CMake FetchContent) fails with DNS errors: either fetch them in
  `setup` (built into the image) or recreate with `network: "full"` and say why.
- **Install packages in the definition**, not by hand with `sandbox_run apt-get install`: a reset or a definition
  change makes a new container and they are gone. Add them to `packages` (`sandbox_create(..., replace: true)`), then
  `sandbox_control(action: "build")`.
- **Package names differ** per distribution (`build-essential` / `gcc gcc-c++ make` / `build-base`); presets map them.
  A name a repository lacks fails the image build with the package manager's words.
- **Windowed programs**: the `gui` preset gives a virtual screen. Run with `xvfb-run -a <program>` (Debian, Ubuntu,
  Fedora have it) or start `Xvfb :99 &` and set `DISPLAY=:99`; OpenGL and Vulkan are Mesa's software renderers
  (llvmpipe, lavapipe) - correct, slow, no GPU. Photograph the screen with `import -window root /workspace/build/shot.png`
  and look at it with `image_view`.
- **A command that never ends** (a server, a game loop) needs a limit: `timeout 20 ./game --smoke-test`, or `timeout_s`.
- **Docker Hub's anonymous pull limit** (an image build failing with "toomanyrequests"): the owner signs Docker in, or
  sets `sandbox.registryMirror` to `mirror.gcr.io/library`.

## What cannot be a sandbox

macOS (only on Apple hardware - its license forbids it in Docker on a PC), Windows desktops and FreeBSD (they need a
full virtual machine, not built yet), Windows containers (Docker's Windows engine, not run by IED yet), real GPUs. Say
so plainly when asked, and offer the closest check that is possible.
