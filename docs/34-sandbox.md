# 34 - Sandboxes: other operating systems and architectures

## Purpose

Run the workspace's program on another operating system or processor than this machine's, to find out whether it
works there - build it, run its tests, start it - without leaving IED and without touching the host. Owner direction
(2026-11-11): "a sandbox plugin ... for sandboxing applications in a VM to test them on different operating systems (and
architecture/hardware if emulateable) to validate that they work on that system like validating that linux works. we
should have tools in place to make it easy to download and set up different setups like operating systems. we can use
docker for this make sure we have proper tools set up in place for the AI to work with this feature."

This doc is the design of `plugins/sandbox` (`ied.sandbox`) as built; `features/11-sandbox.md` keeps the feature
catalog (F-SBX-*) and says which rows are built. It goes past the predecessor's sandbox (one Ubuntu image, one
architecture, `docs/20` row 17) with a catalog of operating systems, emulated processors, and a validation matrix the
AI runs on its own.

## What a sandbox can be

A **sandbox** is a named, committed definition (`.ied/sandbox/<name>/sandbox.json`) of a machine the workspace runs in:
an operating system image, a processor architecture, the packages it needs, and how it reaches the network. The
**docker** provider makes it a container of Docker (Docker Desktop's Linux engine on Windows, which runs inside its own
WSL2 virtual machine), with the workspace's folder mounted at `/workspace`.

| Target | How | State |
|---|---|---|
| Linux distributions (Ubuntu, Debian, Fedora, Alpine, Arch, openSUSE, Rocky, Alma, Amazon Linux) | an official image of the distribution | **built** |
| Other processors: arm64, armv7, armv6, riscv64, ppc64le, s390x | the same images for that platform, run under QEMU user-mode emulation (Docker Desktop's binfmt handlers; more installed on request) - 5-20x slower than native, exact instruction sets, the real libc and kernel ABI of that platform | **built** |
| 32-bit x86 (i386) | `linux/386` images, run natively by a 64-bit x86 engine (no emulation; `uname -m` still says x86_64) | **built** |
| musl instead of glibc | Alpine | **built** |
| Windows (other versions, Server Core, Nano Server) | Windows containers: Docker Desktop's other engine (`docker desktop engine use windows`), a catalog entry with `engine: "windows"` | designed: the catalog marks them and the engine row says how to switch; not run yet |
| FreeBSD, other kernels, a Windows desktop | a full virtual machine under QEMU inside a container (`kind: "vm"`), which needs KVM for useful speed - not available to Docker Desktop on Windows 10 | not built |
| macOS | only on Apple hardware: the `ssh` provider to a Mac (`features/11` F-SBX-8). macOS in Docker on PC hardware breaks Apple's license and is never offered | not built |

Nothing a sandbox does runs on the host: a sandbox whose engine is missing or stopped says so, with how to fix it, and
never falls back to running the command here.

## The plugin

`ied.sandbox` is a built-in plugin (`runtime.kind: "builtin"`, its `code/` a source folder of `ied-server`,
`src/nuke.toml`) whose code runs in each workspace server (`docs/01` "Processes"): a workspace's sandboxes, their
containers and their runs belong to that workspace. It needs `process.spawn` (it runs `docker`), `fs.read` and
`fs.write` (definitions, generated Dockerfiles), `terminal` (its terminal profiles), `ai.tools`.

| Part | Where |
|---|---|
| Engine: docker found, its version, whether it runs, the platforms it emulates, starting Docker Desktop | `code/SandboxEngine.ch` |
| Catalog: operating systems, their versions and platforms, package presets per package manager | `catalog.json`, read by `code/SandboxCatalog.ch` |
| Definitions: reading and writing `.ied/sandbox/<name>/sandbox.json`, the Dockerfile, tag, container and docker arguments made from one | `code/SandboxDefinition.ch` |
| Service: each workspace's sandboxes, their state, the job queue, runs and their output, validation | `code/SandboxService.ch` |
| AI tools, the system-message section | `code/SandboxTools.ch` |
| Protocol methods (`ied.sandbox/*`), bindings, the entry | `code/SandboxPlugin.cf` |
| The Sandbox activity: the Sandboxes, Activity and Validation windows, a sandbox's page, New Sandbox | `ui/` |
| The skill | `skills/sandbox/SKILL.md` |

### The engine

The plugin runs Docker's command-line program, never its API socket: `sandbox.docker` (app tier) is the program and
any leading arguments (`["docker"]` by default; `["docker", "--context", "x"]` for another engine). What the engine
row shows and every tool answers first:

| State | Means | What the owner and the AI can do |
|---|---|---|
| `missing` | no `docker` program | install Docker Desktop (`winget install Docker.DockerDesktop`, shown, never run by IED); the WSL2 backend |
| `stopped` | the program is there, its engine does not answer | **Start Docker** (`docker desktop start`; the AI: `sandbox_engine(action: "start")`); IED waits for it (up to 3 minutes) |
| `starting` | a start IED asked for is under way | wait |
| `ready` | `docker version` answered | everything |
| `failed` | a start did not finish in time, or the engine answered an error | the error as Docker said it |

When ready, the engine row also gives the server's version, operating system and architecture, its processors and
memory, and the **platforms** it runs - native and emulated, from `docker buildx inspect` (Docker Desktop on x64:
`linux/amd64`, `linux/arm64`, `linux/riscv64`, `linux/ppc64le`, `linux/s390x`, `linux/386`, `linux/arm/v7`,
`linux/arm/v6`). A platform the engine lacks is offered as **Install emulators**: `docker run --privileged --rm
tonistiigi/binfmt --install all` (asks the owner first: it changes the engine's virtual machine and downloads an image).
The engine is looked at when the window opens or is refreshed, and before every job (a ready engine's look is kept
15 s); a look is `docker version` (well under a second when it answers, 20 s at most), and the first one of an engine
version also reads its processors, memory and platforms (`docker info`, `docker buildx inspect`).

### The catalog: setups to download

`plugins/sandbox/catalog.json` lists what a sandbox can be made from, so neither the owner nor the AI has to know image
names, tags or package managers:

```json
{ "id": "ubuntu", "title": "Ubuntu", "family": "debian", "packageManager": "apt",
  "versions": [ { "version": "24.04", "image": "ubuntu:24.04", "title": "24.04 LTS (Noble)", "default": true,
                  "platforms": ["linux/amd64", "linux/arm64", "linux/arm/v7", "linux/ppc64le", "linux/riscv64", "linux/s390x"] } ] }
```

and **presets** - named sets of packages - per package manager (`apt`, `dnf`, `apk`, `pacman`, `zypper`, `yum`):
`build` (a C/C++ compiler, make), `cmake`, `ninja`, `git`, `python`, `node`, `go`, `rust`, `java`, `dotnet` where the
distribution has it, `debug` (gdb, strace, valgrind where it builds), `gui` (Xvfb, Mesa's software OpenGL and Vulkan,
xdotool and ImageMagick, so a windowed program can run and be photographed headless), `net` (curl, ca-certificates,
iproute). A preset a distribution lacks is reported when the sandbox is created, never silently dropped.

A version is **downloaded** when the engine holds its image for that platform (`docker image inspect`); the window
shows it, with its size, and the catalog has **Download** (`docker pull --platform`) and **Remove**. Owner-added
entries come from `sandbox.images` (app tier, the catalog's shape) and are listed after the shipped ones, an entry of
the same id replacing a shipped one.

### Definitions

A sandbox is the folder `.ied/sandbox/<name>/` (`docs/03` "The .ied folder"; committed, so a clone has the same
sandboxes) with `sandbox.json`:

| Field | Default | |
|---|---|---|
| `os` | - | a catalog id and version, `ubuntu:24.04` (or `image`) |
| `image` | from `os` | any image reference, for what the catalog lacks (`os` then only names it) |
| `platform` | the engine's own | `linux/arm64`, ... |
| `presets` | `[]` | catalog presets |
| `packages` | `[]` | more packages, by the distribution's names |
| `setup` | - | shell lines run as root when the image is built, after the packages (`RUN` lines) |
| `env` | `{}` | environment of every run |
| `validate` | - | the command `sandbox_validate` and the window's Validate run when they name none (`cmake -B b && cmake --build b && ctest --test-dir b`) |
| `network` | `"none"` | at run time: `none` (deny all) or `full`. Building the image always has the network (packages) |
| `cpus`, `memoryGiB` | `sandbox.defaultCpus` (4), `sandbox.defaultMemoryGiB` (6) | the container's limits |
| `description` | - | one line the window and `sandbox_list` show |

`name` is the folder's name: lowercase letters, digits and `-`, at most 40. A `Dockerfile` in the folder replaces the
one IED makes (the owner's own image; `presets`, `packages` and `setup` are then ignored, and `sandbox_list` says so).

**The image.** IED writes the Dockerfile it builds to `.ied/local/sandbox/<name>/Dockerfile` (`FROM <image>`, one
`RUN` that installs the presets' and the definition's packages with the distribution's package manager -
non-interactive, its caches removed - then `setup`'s lines, `WORKDIR /workspace`) and tags the image
`ied-sandbox/<workspace key>-<name>:<digest>`, the digest being the first 12 hex digits of SHA-256 over the Dockerfile
and the platform. A changed definition makes a new digest and so a new build; an unchanged one reuses the image
Docker has. Building streams Docker's output to the run's output.

**The container** is `ied-<workspace key>-<name>` (the key: 8 hex digits of the workspace root's hash, so two
checkouts never share one), started with `docker run -d --init --platform <p> --label ied.sandbox=<name> --label
ied.workspace=<root> --label ied.sandbox.config=<digest> -v <root>:/workspace -v <root>/.ied:/workspace/.ied:ro
-w /workspace --cpus <n> --memory <n>g --pids-limit 4096 --security-opt no-new-privileges [--network none] <image>
sleep infinity`. Its config digest also covers the mount, limits and network, so a change to any of them recreates it;
otherwise it is reused - its filesystem outside `/workspace` (installed caches, a home folder) persists until a reset.
A sandbox starts when a run needs it (lazy) and stops after `sandbox.idleStopMinutes` (30) without a run. Only the
workspace's folder is mounted, its `.ied/` read-only (a run cannot change the sandbox definitions, settings or skills
that steer it - the predecessor's rule): no home folder, no SSH keys, no Docker socket. Docker's default capability
set stays (package managers need it); `no-new-privileges` keeps setuid programs from gaining more.

**Build outputs.** The workspace is shared with the host, so a Linux build writing to the same `build/` folder as a
Windows build would mix them, and a validation runs several sandboxes at once on the same folder. Every run has
`IED_SANDBOX` (the sandbox's name) and `IED_PLATFORM` (`linux/arm64`) in its environment, so one command builds into a
folder of each sandbox's own (`cmake -B build/$IED_SANDBOX`); build systems that name the platform in their output
folders (Nuke's `build/bin/<config>-<system>-<arc>`) need nothing - the skill says so.

### Jobs and runs

A job (build an image, start, stop, reset, remove, pull, a run) runs on a thread of its own - a tool's on the tool
call's, a window's on one of the plugin's, so the method answers at once and the job goes on with the browser closed.
A sandbox's own lock keeps two jobs from building or starting one sandbox at once (a validation and a run asking for
the same image build it once); commands run side by side, in one sandbox or several. A validation runs
`sandbox.parallelRuns` (2) sandboxes at a time. Each job is a **run** the window lists, its output kept.

A **run** is `docker exec -w /workspace/<cwd> -e ... <container> sh -lc <command>` under a wrapper that records the
shell's process id, so stopping a run (the owner's Stop, a chat's stop, a time limit) ends the command inside the
container, not only the `docker` client. Its environment has `IED_SANDBOX` (the sandbox's name) and `IED_PLATFORM`.
Its output streams to the workspace's **Sandbox** output source (whole lines, each prefixed with `[<sandbox>]`, since
several run at once) and to the topic; the last 64 KB is kept with the run.
The exit code, the time, and whether it was stopped are the run's result.

**Paths.** `/workspace/` in a run's output is the workspace's root: the Output window's links resolve it (the plugin
contributes an `outputLinks` pattern for `/workspace/<path>:<line>`), and the AI is told so.

### Validation

The point of a sandbox: **does it work there?** `sandbox_validate` (and the window's **Validate**) runs one command -
the argument, else each sandbox's `validate` - in several sandboxes (the named ones, else all) and answers a matrix:

```
sandbox      os            platform       result   time    
ubuntu       ubuntu:24.04  linux/amd64    pass     41 s
ubuntu-arm   ubuntu:24.04  linux/arm64    FAIL 2   3 m 12 s   tests/endian.c:41: expected 0x01020304
alpine       alpine:3.20   linux/amd64    FAIL 1   12 s       error: 'qsort_r' undeclared (musl)
```

with each failing run's last lines. The last validation of each workspace is kept (`.ied/local/sandbox/validation.json`)
and drawn in the window, so a closed browser or a restarted server loses nothing.

### Terminals

Each sandbox is a terminal profile (computed per workspace, `docs/05` "Terminal profiles"): "Sandbox: ubuntu-arm" runs
`docker exec -it -w /workspace <container> sh -lc "exec bash -l || exec sh -l"` under the terminal dock's pseudo
console. The window's **Terminal** starts the sandbox first, then opens it.

## AI tools

Group `sandbox` (deferred: the chat loads them by name, `docs/13` "Tool search"). Every tool answers the engine's state
first when it is not ready, with what to do; none runs anything in an untrusted workspace.

| ID | Tool | Class | Does |
|---|---|---|---|
| T-SBX-1 | `sandbox_list()` | read | The engine (state, version, platforms) and each sandbox: OS, platform (emulated or native), presets, network, state (no image / image built / running / stopped), its last run and validation. |
| T-SBX-5 | `sandbox_catalog(query?)` | read | The operating systems and versions it can make sandboxes of, with their platforms and whether each is downloaded; the presets and what they install per package manager. |
| T-SBX-6 | `sandbox_create(name, os?, image?, platform?, presets?, packages?, setup?, validate?, network?, cpus?, memory_gib?, description?, replace?)` | write | Writes `.ied/sandbox/<name>/sandbox.json` (checked against the catalog and the engine's platforms) - the approval card shows the file as a diff. Builds nothing yet. |
| T-SBX-2 | `sandbox_run(name, command, cwd?, timeout_s?, env?)` | exec | Builds the image and starts the sandbox as needed (that output summarized), runs the command, answers its exit code, time and the output's end, `/workspace` named as the root. |
| T-SBX-7 | `sandbox_validate(command?, names?, timeout_s?)` | exec | The validation matrix above. |
| T-SBX-8 | `sandbox_control(name, action)` | exec | `build`, `rebuild` (no cache), `start`, `stop`, `reset` (a new container from the image), `remove` (container and image; the definition stays), `pull` (download the base image). |
| T-SBX-9 | `sandbox_engine(action?)` | exec | `status`; `start` (Docker Desktop); `install_emulators`. |
| T-SBX-10 | `sandbox_screenshot(name, command?, wait_s?)` | exec | Not built. Runs a windowed program under Xvfb (the `gui` preset) and shows the screen as an image. |
| T-SBX-3 | `sandbox_task(name, task)` | exec | Not built: needs task targets (below). |
| T-SBX-4 | `sandbox_request_network(name, host, port, reason)` | net | Not built: needs the egress gateway (below). |

The system-message section (a `function`, only when the workspace has sandboxes) lists them in one line each and
names the tools; the `sandbox` skill teaches the loop: look at the engine, pick from the catalog, create, validate, read
a failure, fix it on the host, validate again on every sandbox - and the caveats (emulation speed, build folders, the
network being off, GUI programs needing `gui`, what cannot be a sandbox).

## The Sandbox activity

Sandbox has a button of its own in the sidebar (after GPU), a dock host like the Profiler's (owner, 2026-11-11: "it
should be its own tab in the side bar where i can see what the AI is doing in the sandbox"; it was a tab of the
editor's well before). Laid out like Visual Studio's tool windows: dense rows, no decoration, grayscale with the
semantic accents only - the AI's accent for what the AI started, running and pass green, busy blue, failed red.

- **Sandboxes** (a tool window, docked left): Docker's state and version with the one action the state needs (Start
  Docker, how to install Docker), the platforms it runs (native ones marked), New and Validate All, then the sandboxes -
  name, system, platform ("emulated"), state, the last validation's result, and an **AI** mark while the AI runs
  something in one. A sandbox opens as a document of its own.
- **Activity** (a tool window in the document well, where the activity opens): **what the AI is doing.** A strip on
  top says what runs now and who started it ("The AI ("Fix the packer") is running `make test` in alpine-s390x"); the
  runs, newest first, each with who started it - **AI** with its chat, **You**, or a plugin - its sandbox, command,
  state and time, filtered by All / AI / You; the console shows the run picked, else follows the newest one going (the
  AI's first), its output streaming in, with Stop and **Open chat** (the chat the AI ran it from).
- **Validation** (a tool window in the document well): the last validation's pass/fail matrix, a failing row's last
  lines under it; a row shows its run in Activity.
- **A sandbox** (`sandbox:box/<name>`, a document): its actions (Build, Start, Stop, Terminal, Run..., Validate, Reset,
  Remove, `sandbox.json`), its definition, image and container, and its runs with the console.
- **New Sandbox** (`sandbox:new`, a document): operating system and version from the catalog (downloaded ones marked,
  Download), platform (the version's platforms the engine runs; emulated marked; Install emulators when Docker lacks
  one), presets as checkboxes, extra packages, the validate command, network, name; Create writes the definition, Create
  and Build also builds it.

The sidebar button carries a badge - how many commands the AI is running in sandboxes - so the owner sees the AI at
work from any activity. Every run records who started it (`by`: `ai` with the chat's id, `owner`, `plugin`), and the
Output window's Sandbox source marks the AI's with "(AI)". On a phone the activity's windows are panes; Activity shows
the runs, then a run's console. Everything shown is the server's topic `sandbox:<wsId>`, shared by the views through
one subscription; the browser holds nothing, and a run goes on with every browser closed.

## Protocol and bindings

Topic `sandbox:<wsId>`: snapshot and `changed` events `{wsId, root, key, engine, dockerProblem, sandboxes: [...], runs: [{id, sandbox, by, chatId, kind, label, running, exitCode, stopped, problem, startedMs, endedMs, output?}], validation}` (outputs only in the pulled snapshot, the newest runs'); `output` events `{runId, text}`.
Methods (the window's, each with `wsId`; a job answers `{queued}` at once and goes on on a thread of the plugin's):
`ied.sandbox/state` (the snapshot, and a fresh look at Docker), `ied.sandbox/catalog`, `ied.sandbox/create`,
`ied.sandbox/control {name, action}`, `ied.sandbox/run {name, command}`, `ied.sandbox/validate {names?, command?}`,
`ied.sandbox/stopRun {id}`, `ied.sandbox/engine {action: start | emulators | refresh}`, `ied.sandbox/pull {image, platform}`,
`ied.sandbox/removeImage {image}`. Bindings for other plugins (AGENTS.md rule 7):
`sandbox.list` (`{wsId}` -> the snapshot) and `sandbox.run` (`{wsId, name, command, cwd?, timeoutSeconds?}` -> `{queued}`,
its output and end on the topic: a binding answers within 30 s, a build does not).

## Settings

| Key | Tier | Default | |
|---|---|---|---|
| `sandbox.docker` | app | `["docker"]` | the Docker program and leading arguments |
| `sandbox.defaultCpus` | app, solution | 4 | |
| `sandbox.defaultMemoryGiB` | app, solution | 6 | |
| `sandbox.idleStopMinutes` | app | 30 | 0: never |
| `sandbox.parallelRuns` | app | 2 | runs at once in a workspace |
| `sandbox.runTimeoutSeconds` | app | 1800 | a run's default limit (tools take `timeout_s`) |
| `sandbox.images` | app | `[]` | catalog entries of the owner's |

## Security

The workspace must be trusted (`docs/11`): a definition is project-defined code (its `setup` lines, its `Dockerfile`).
The container sees only the workspace folder; its network is off unless the definition turns it on, and turning it on
is a committed change the owner sees (and approves, when the AI makes it). `install_emulators` runs a privileged
container and asks every time (`approval: "ask"`). Images are pulled from the registry their reference names (Docker
Hub for the catalog's official images).

## Not built yet

- **Egress gateway** (F-SBX-10/11/12, predecessor parity): `network: "gateway"` - the sandbox on an internal network
  whose only way out is a proxy container (DNS, HTTP CONNECT, SOCKS5 with remote DNS), every resolved address checked
  against loopback, private, link-local, CGNAT, metadata and reserved ranges, grants as exact host:port re-read on each
  dial; `sandbox_request_network` approval cards (the predecessor let only the owner grant); through Tor/proxy profiles.
- **GUI desktop** (F-SBX-24, predecessor parity): Xvfb + a VNC server + noVNC in the image (`gui` preset), served to the
  window through the workspace server, with take-over; first `sandbox_screenshot` for the AI.
- **Task targets** (F-SBX-21): a task step's `target: "sandbox:<name>"`, the task runner asking the `sandbox.wrap`
  binding for the argv (host stays generic: any plugin may provide a target kind).
- **Other providers**: `wsl`, `ssh` (a Mac, an ARM box), `vm` (QEMU), Windows containers; **copy mode** (a copy of the
  workspace instead of the mount, for full isolation and speed); persistent cache volumes.
