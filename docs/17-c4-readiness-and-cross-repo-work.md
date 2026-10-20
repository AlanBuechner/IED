# 17 - C4 readiness and work outside the IED host

## Purpose

Resolve `docs/99` #5 as far as research can: **what exactly must exist in C4 / Bedrock before the IED server can be written in C4**, what the family's other tools (C4LSP, C4DAP, Nuke, C4Assist, game engines built on C4) must change for IED, and in which order. Facts below come from reading the C4 tree (now `C4/` in this repo) and a C4 game-engine port on 2026-10-19; re-verify before acting (the compiler moves fast).

## Where C4 stands (2026-10-19)

- Re-architecture (ProgramModel -> HIR -> codegen) is done; gates are green (TestCode 1505 assertions, NegativeTest 564, IsolatedTest 374 at -O0 and -O2, Bedrock 290 tests, C4Testing 964). `C4/docs/status/known-bugs.md` lists no open bugs. Windows x64 only; Linux port (B31) planned; ORCv2 scripting (B30) at spike stage.
- Largest C4 codebases so far: Bedrock ~10.9k lines, TestCode ~9.4k, a game-engine port ~4.2k. **Nothing near the IED server's expected size (40-80k lines) has been compiled.** Whole-program compile, no incremental builds yet (estimated 15-20 s at -O2 for 50k lines).

## Capability gap table (server needs vs. C4 today)

| Need | Status | Notes |
|---|---|---|
| Threads, mutex, condvar, atomics, futures, job system | **exists** | `C4/Bedrock/Threading/*`; JobSystem is game-tuned, not I/O-aware |
| JSON parse/serialize | **exists** (DOM) | No streaming parser; reflection-driven serialization of any type with strings, arrays, string-keyed maps and enum names: `Serialization::ToJson` / `FromJson` (C4 B56) |
| UTF-8 strings | **exists** | UTF-8 <-> UTF-16 conversion and UTF-16 offsets for LSP positions: `Core::ToUtf16` / `FromUtf16` / `Utf16OffsetOfByte` / `ByteOffsetOfUtf16` (C4 B60) |
| File system basics | **exists** | read/write/list/mkdir/remove/rename/exists/size, `FS::GetInfo` (kind, size, times), copies of files and trees, absolute and canonical paths (C4 B53); **missing** locking, mmap |
| File watching | **exists** | `FS::Watcher` over `ReadDirectoryChangesW` (B53): recursive, renames paired, `Overflow` when changes were dropped |
| TCP sockets, event loop (IOCP), timers | **exists** | `Net::` TCP, UDP, resolution, `Poll`, blocking calls with timeouts (C4 B49); `Net::EventLoop` with socket watches, timers and work posted from other threads (B54, readiness-based over `WSAPoll`; tested with 1000 connections on one loop) |
| HTTP/1.1, WebSocket, SHA-1, base64, TLS | **exists** except TLS | `Http::` server (thread per connection) and client (B50), WebSockets (B51), `Core::Sha1`, `Core::Base64*` |
| Child processes with pipes, job objects, env vars | **exists** | `Process::Child` / `Process::Run` (C4 B52): pipes with timeouts, `ReadAny`, job objects (kill the tree, kill on close), environment changes; env get/set in `Platform::` (B40) |
| ConPTY | **exists** | `Process::StartInfo::terminal` (B52): keyboard in, VT screen out, `Resize` |
| Wall-clock time / dates | **exists** | `Platform::UnixTimeNs`, calendars and UTC offsets (B40) |
| Regex | **exists** | `Core::Regex` (C4 B55): RE2 syntax, linear-time Pike VM, groups, `FindAll`, `Replace` |
| C interop | **exists** | the suspected bugs below were confirmed and fixed (C4 B57-B59): records keep C's layout, header functions and `#define` constants bind |
| Compiling bundled C sources | **exists** | `extern "c" source` compiles at the build's `-O` with its `-g` and is reused while unchanged (C4 B59) |
| DLL output / loading C4 DLLs | **missing / not designed** | GC has process-global state; native plugins as C4 DLLs are not a path (hence `docs/02`: built-ins compiled in, others JS or process) |
| async/await | **missing** | not required: an event-loop + worker-thread design works without it |
| Profiler hooks | **partial** | `GC::GetStats`, collection callbacks, monotonic clock, CodeView PDBs; no zone API, no allocation hooks |

GC facts that shape server design: stop-the-world, conservative, non-moving; pause ~6 ms per 50 MiB live heap with 8 markers; threads created by native code must call `thread_attach()` before touching C4 objects; native memory holding C4 pointers must be registered or pinned; **an exception escaping a thread or job kills the process** (so every worker entry needs a catch-all, F-OPS-11).

## Suspected C4 bugs found during research (verify, then file + fix per AGENTS.md)

Found by reading CBind source. **Status (2026-10-20): every one was confirmed with a probe and fixed** - S1-S4 as BUG-279 to BUG-282 (C4 B57, CBind now writes records from clang's layout), S5-S6 as BUG-283 and BUG-284 (B58), S7's missing optimization and cache as BUG-285 (B59; its same-stem collision was BUG-272). The table is kept as the record of what was suspected.

| # | Suspected bug | Where (per reading) | Impact on IED |
|---|---|---|---|
| S1 | Named C unions are emitted as `struct`, so overlapping members are laid out sequentially (wrong size/offsets) | `CBind BindingGenerator.cpp:34` | QuickJS `JSValue`, Win32 `LARGE_INTEGER`, many socket/IOCP structs |
| S2 | Unmappable field types fall back to `void*` (8 bytes) regardless of real size | `CBind C4Text.cpp:132` | `in_addr.S_un` (4 bytes) breaks `sockaddr_in` |
| S3 | Anonymous unions expose only their first member and lose their full size | `C4Text.cpp:144-164` | `OVERLAPPED`, many Win32 structs |
| S4 | Bitfields are not handled | CBind | some Win32/third-party structs |
| S5 | `static inline` functions in headers get no link target or wrapper (link errors) | `BindingGenerator.cpp:96-104`, `BindingShim.cpp:92-99` | QuickJS, many single-header libs |
| S6 | `#define` constants are not bound | interop test defines `WM_CLOSE` by hand | every Win32 constant (`AF_INET`, `INVALID_SOCKET`, ...) |
| S7 | Bundled C sources compiled without optimization and without an object cache; two sources with the same file stem collide | `BuildUtils.cpp:223-264`, `InteropPass.cpp:494-511` | QuickJS/sqlite builds slow and unoptimized |

With S1-S6 fixed, IED may bind native headers directly. **IED's policy stays: OS services come through Bedrock (`Net::`, `Process::`, `FS::`), and a third-party native library goes through a small C shim with opaque handles and plain scalar/pointer signatures** - not because of CBind any more, but because one tested seam per subsystem is easier to own.

## Recommended path: what goes where

***Bedrock (`C4/Bedrock/`) should own OS primitives**, because game engines need most of them too (file watching for hot reload, processes for tools, sockets for the session pipe and profiler):

| Bedrock module (proposed) | Contents | Also needed by |
|---|---|---|
| `Net::` | TCP listen/accept/connect, non-blocking sockets, an IOCP-backed event loop with timers, UDP (profiler discovery) | engine remote control, profiler client |
| `Process::` | spawn with pipes, job objects (kill tree), exit codes, environment get/set, ConPTY sessions | engine tools, Nuke-in-C4 later |
| `FS::Watch` + FS extras | `ReadDirectoryChangesW` watcher, stat/mtime, copy, canonical paths | engine hot reload |
| `Time::` | wall clock, UTC/local formatting, durations | everyone |
| `Text::` additions | UTF-16 conversion, regex (RE2-like subset or a vendored C engine behind a shim), base64, SHA-1/SHA-256 | everyone |
| `Serialization::` | reflection serialization for `String`, `Array`, `HashMap`, enums; streaming JSON writer | IED protocol DTOs, engine assets |
| `Profile::` | zone/frame/plot/message/alloc/thread-name/GPU APIs speaking the IED trace protocol (`docs/16`) | game engines, every C4 app |

**IED (this repo) owns** HTTP/1.1 + WebSocket on top of `Net::` (or a vendored C HTTP parser such as llhttp behind a shim), the protocol hub, and everything in `src/`.

An alternative that gets IED unblocked sooner: wrap **libuv** (MIT, C; IOCP event loop, TCP, timers, processes with pipes, fs events, thread pool) as a prebuilt `.lib` behind a C shim, and build `Net::`/`Process::`/`FS::Watch` as thin C4 layers on it. ConPTY still needs its own small shim. The C4 team decides; the IED server only depends on the Bedrock-level API either way.

## Readiness checklist ("the compiler can build IED")

Implementation of `src/` starts when all of these are true. Each item is a work package in the C4 tree (`C4/`).

1. [x] `Net::` sockets + event loop + timers, with tests (echo server, 1k concurrent connections, timers under load). Done in C4 B49 and B54 (D-65, D-70).
2. [x] `Process::` spawn/pipes/job objects/env + ConPTY, with tests (spawn `cmd /c`, kill tree, PTY round trip). Done in C4 B52 (D-68).
3. [x] `FS::Watch`, stat/mtime, copy, canonical paths. Done in C4 B53 (D-69) as `FS::Watcher`, `FS::GetInfo`, `CopyFile` / `CopyTree`, `CanonicalPath`.
4. [x] `Time::` wall clock; UTF-16 helpers exposed; base64 + SHA-1 (WebSocket handshake). Done: wall clock (`Platform::`, B40), base64 and SHA-1 (B51), UTF-16 conversion and LSP-style offsets (`Core::ToUtf16` / `FromUtf16` / `Utf16OffsetOfByte` / `ByteOffsetOfUtf16`, B60).
5. [x] Regex engine available to C4 code. Done in C4 B55 (D-71).
6. [x] Reflection serialization for String/Array/HashMap/enum (or IED writes DTO codecs by hand - decide). Done in C4 B56 (D-72): DTOs need no hand-written codecs.
7. [x] Suspected bugs S1-S7 verified; confirmed ones fixed (or the C-shim-only policy formally adopted and S1-S6 deprioritized). All confirmed and fixed in C4 B57-B59 (BUG-279 to BUG-285): named and anonymous unions, unmappable fields, bitfields, header functions, `#define` constants, foreign-source optimization and caching.
8. [ ] **Scale test:** a synthetic 50k-line, 200-file, multi-threaded C4 program compiles at -O0 and -O2, runs a 24 h soak under load without GC or threading faults; compile time measured. C4's `assets/ScaleTest` (60k lines, 401 files) compiles in 4.4 s at -O0 and 15.0 s at -O2 and ran 60 s on 4 threads without a wrong result; the 24 h soak is still to run (`python C4/assets/ScaleTest/run.py --seconds 86400 --levels O2`).
9. [x] Exceptions-at-thread-boundary policy documented (catch-all wrappers), `thread_attach` documented for native callbacks. Done in C4 B60 (Bedrock's Threading section: an exception leaving a `Thread::Run` body, a job or an event loop's `Run` ends the process as in C++, so a server wraps each entry in `try { ... } catch (...)`; a thread native code started calls `thread_attach()` before it touches C4 objects and `thread_detach` before it ends).
10. [x] Incremental or at least per-module compilation planned (roadmap 5.3), because a 20 s full rebuild for every IED server change is a heavy tax. Done in C4 B61 (D-76): Nuke's Debug and Development builds pass `--incremental`, which optimizes each object alone and reuses the unchanged ones; a one-module edit of the 60k-line ScaleTest at -O2 rebuilds in 2.6 s (whole program: 12.3 s) with the same run speed. Front-end caching is the next step (roadmap 5.3).

Until then, the **frontend (`frontend/`, `shared/ui-kit`) and the contract (`shared/host-api`) can be built and tested against a mock server** - they do not depend on the compiler (`docs/18`, M0).

## Work items in C4, Nuke and engine tools

### C4LSP (`C4/C4LSP/`)

Today: stdio, full sync, standard features (completion, hover, signature help, def/decl/type-def/impl, references, highlights, symbols, semantic tokens full, rename, formatting, code actions, folding), push diagnostics matching the compiler exactly. Legacy quirks IED's generic client must tolerate now (the C4 plugin's LSP profile declares them, see `shared/host-api/examples/c4-plugin`) and the C4 team should fix:

| # | Item | Why |
|---|---|---|
| L1 | Honor `general.positionEncodings` (offer `utf-8`; today columns are effectively bytes, correct for ASCII only) | Correct positions for non-ASCII text |
| L2 | Accept string JSON-RPC ids (today answered with id -1) | Spec compliance |
| L3 | Implement `$/cancelRequest` | Typing fast cancels stale completions |
| L4 | Send `workspace/semanticTokens/refresh` (and folding/inlay refresh) after recompiles, or support `semanticTokens/full/delta` | Removes the client refetch-after-diagnostics hack |
| L5 | `codeAction` honoring the request range/context | Removes client-side filtering |
| L6 | `completionItem/resolve`, MarkupContent hovers (markdown) | Richer IntelliSense |
| L7 | Inlay hints, call hierarchy, codeLens (tests: "Run / Debug"), `workspace/willRenameFiles` | VS parity features (F-EDT-21, F-BLD-43) |
| L8 | Read config from IED's `.ied/local/lsp-options/c4.json` (via `initializationOptionsFile` - no LSP change needed) | Bridge retarget |
| L9 | Incremental re-analysis (already on the C4 roadmap) | Latency on large projects (IED itself) |

### C4DAP (`C4/C4DAP/`)

Today: launch, line breakpoints, thrown/uncaught exception filters, continue/step/pause, threads, stack, one Locals scope, variables, limited evaluate. For VS-class debugging (F-DBG-*): conditional + hit-count + log breakpoints; setVariable; attach; function breakpoints; `readMemory`; `disassemble`; modules; Autos-friendly scopes; `evaluate` contexts (watch/hover/repl). Prioritize conditional breakpoints, attach and memory.

### Nuke (`Nuke/`; copies vendored into game projects may be older)

| # | Item | Why |
|---|---|---|
| N1 | **Done 2026-10-20** (`Nuke/nuke/c4/ide_bridge.py`, `docs/23` N-11): `IEDConfig.py`: write `.ied/local/lsp-options/<proj>.json` after each successful build (same content as the `c4-lsp.json` it writes today for the predecessor IDE: includePaths, defines, c4Home, projects) | LSP/build sync (`docs/06`) |
| N2 | `nuke init --ied`: write `<Name>.ied.sln`, generated `.ied.proj` files under `build/ied/`, `.ied/` scaffold (skills, solution.json), and the local plugin pin - idempotent, never clobbering hand edits | J7, F-WSP-60 |
| N3 | **Done 2026-10-20** (compiler output streams line by line): **Stream compiler output** instead of printing it after the compile finishes | Live Build output |
| N4 | **Done 2026-10-20** (`nuke test --filter GLOB`; every harness writes `<int>/tests/results.json`): Pass `-test-json <path>` and `--filter <glob>` through `nuke test` (`--json-results`, `--filter`) | Test Explorer (F-BLD-40) |
| N5 | Machine-readable diagnostics: `--diagnostics-json <path>` (file, line, col, severity, message, code) **with absolute paths** - today names are relative to their owning source root and can collide | Error List without regex guessing (F-BLD-33) |
| N6 | **Done 2026-10-20** (`nuke list --json`; configurations, toolchain and commands in `nuke info --json`): `nuke list --json` (projects, kinds, configs, archs) | Toolbar dropdown `valuesFrom` |
| N7 | Optional ANSI color in Nuke/compiler output when `IED_COLOR=1` / a TTY is detected | Output window colors |
| N8 | **Superseded 2026-10-20**: workspaces no longer vendor Nuke or the compiler; they pin a shared toolchain (`docs/23` N-7) and their manifests become `nuke.toml`. Was: Bring vendored copies in game projects up to date (exit codes, `-g` for debugging, `--projects`) | F5 on a game project needs PDBs; tasks rely on exit codes |
| N9 | **Retired 2026-10-20**: the rebuilt Nuke writes only IED's bridge. Was: Keep writing the predecessor IDE's legacy workspace folder for now (it may still be used) but nothing in C4 depends on it anymore (owner note on `docs/99` #10) | Transition |

### C4Assist -> the `c4` IED plugin (C4 tree)

C4Assist (the C4 plugin for the predecessor IDE, Go + TypeScript) was not carried into this repository when C4 moved in (2026-10-20); this section is the record of what it did and what the rewrite needs. Its theme values live on in `frontend/css/theme.css`.

C4Assist is mostly declarative (LSP profile, Visual Assist-style token palettes, editor menus, ctrl-click navigation, auto-close pairs, 9 feature toggles, `c4.kitStatus`/`c4.formatDocument` RPC, one AI tool) plus a tiny Go backend. The rewrite is **a declarative IED plugin with no backend** (see `shared/host-api/examples/c4-plugin/ied-plugin.json`):

- Language definition + Monarch grammar; semantic token mapping to `--syntax-*` tokens (IED's default palette is the Visual Assist X one C4Assist ships, `docs/07`).
- LSP profile (staged launch, byte positions, integer ids, refetch-on-diagnostics, client-side code-action filtering) and DAP provider.
- Problem matchers (`c4`, `c4-test`), test provider (`c4-test-json`), Nuke task preset, project templates, header/source pairs.
- Feature toggles become plugin settings + `when` clauses (no code).
- Skills: the five Nuke-shipped skills (`c4-language`, `nuke-build-system`, `c4-testing`, `c4-reflection`, `c4-garbage-collector`) move to the plugin's `skills/`.
- `kitStatus` becomes a status bar item fed by a binding the host already has (file exists + LSP state); `formatDocument` is the LSP's formatting (no separate tool).
- Port C4Assist's 17 Playwright e2e specs to IED's frontend e2e harness (`docs/19`).

### Bedrock profiler hooks (`C4/Bedrock/`)

`Profile::` module implementing the client side of the IED trace protocol (`docs/16`): zones (scoped), frames, plots, messages, thread names (renamable), task begin/end, allocations (hooked into the GC allocator: alloc/free/size/pool, optional call stacks), GC collection events (from the existing collection callbacks), GPU contexts and zones (for D3D12 engines). Compiled out entirely unless a define is set (zero cost in Dist). Enabled by the IED *Profile* task through an environment variable.

### Bedrock `Gpu::` (`C4/Bedrock/`, for the GPU debugger)

Two steps (`docs/22`). First, small: `Gpu::Capture` (`NextFrames(n)`, `Begin/End`, `SetComment`) over whichever capture tool is loaded - RenderDoc's in-app API (`renderdoc_app.h`, found with `GetModuleHandle`), PIX's programmatic capture, or nothing - plus marker scopes shared with `Profile::Gpu::Zone`. Later (P-8): the Flashbang recorder - a layer under the program's D3D12 calls that writes `.flash` captures, with `Gpu::Annotate` for bindless slot -> asset, material -> source, draw -> entity metadata, and a rolling frame ring. Compiled out unless enabled, like `Profile::`.

### Game engines (for later engine plugins)

The C4 game-engine port's plans already align with IED's generic hooks: a named-pipe session + an engine CLI with `--json`/`--describe` (-> engine MCP server or plugin bindings and tools), structured shader diagnostics (-> problem matcher / diagnostics JSON), JSON profile traces (-> profiler import, then the native trace protocol), structured logs (-> output source), hot reload (-> task + status bar), `.c4scene`/`.meta.c4`/`.jrn` files (-> file viewers, diff). Recommendation to the engine track: emit profiler data through Bedrock `Profile::` rather than a separate JSON format, so one profiler serves both.

## Sequencing (C4 tree)

```
P-1  Bedrock Net + Process + FS.Watch + Time + Text extras   (gates IED server)
P-2  Suspected bugs S1-S7 verify/fix                         (gates any binding work)
P-3  Scale test + exception/thread policy                    (gates IED server)
P-4  Nuke N1-N6 + C4 plugin rewrite (declarative)           (gates the C4 experience in IED; can start as soon as schemas are frozen)
P-5  C4LSP L1-L5, C4DAP conditional/attach/memory            (quality; parallel with IED M2-M4)
P-6  Bedrock Profile:: + trace protocol                      (gates IED profiler M9)
P-7  Linux target (B31)                                      (makes the sandbox valuable for C4 projects)
P-8  Bedrock Gpu:: capture API, then the Flashbang recorder (gates GPU debugger M12 triggers, M13 Flashbang)
```
