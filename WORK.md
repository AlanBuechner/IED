# WORK.md - current state and next actions

Living document. Update it at every checkpoint commit.

## Current state (planning complete for v1 scope; M0 in progress; C4 and Nuke in this repository)

- [x] `IDEplan.md` - vision (owner-maintained).
- [x] Folder structure, root conventions (`README.md`, `AGENTS.md`, `.gitignore`).
- [x] **C4 moved into this repository (2026-10-20)** - `C4/` (compiler, LSP, DAP, runtime, Bedrock, tests, docs, Frost) imported without its old history; the old editor plugin and per-workspace editor state dropped; vendored Tracy and yaml-cpp became submodules (the owner's forks, at the vendored commits); the glm copies stay vendored (locally modified 0.9.9.6). Builds and passes every gate from here: C4Testing 1068 + 91 + 33, e2e 1505 at -O0 and -O2, negative 603 x2, isolated 406 x2, runtime, script x2, tools 27, Bedrock 340.
- [x] **Nuke rebuilt (2026-10-20)** - `Nuke/` (`Nuke/README.md`, design `docs/23`): TOML manifests, chained commands, phases + steps, workspace commands and Python extensions, the shared versioned toolchain store (`toolchains/c4/<version>`, `dev`), test harnesses separate from builds, the IED LSP bridge; 104 tests (`python -m unittest discover -s Nuke/tests`). This repository is a Nuke workspace: `nuke build` / `test` / `gates` / `install-c4` build and check the compiler.
- [x] **Public-repo setup (2026-10-20)** - `setup.bat` + `tools/setup/` (pinned, SHA256-checked downloads in `deps.toml`; private Python; LLVM; web libraries; optional PATH registration), submodules for codicons/libgit2/quickjs-ng, no local paths or names of other private projects in the tree.
- [x] **User stories and journeys** - `docs/08` (personas, journeys J1-J14, 178 stories in 24 epics, tagged plan/plan+/ext and P0/P1/P2).
- [x] **Full feature catalog** - `docs/09` + `docs/features/` (16 areas, 546 features - 247 P0, 240 P1, 59 P2 - with owners and story links; "surpass the predecessor" differentiators; 89 AI tools in `docs/13`).
- [x] **Blocking decisions resolved as drafts** - protocol v1 (`docs/10`), security and multi-client (`docs/11`), manifest/solution/project/federation/agent-mode/frames schemas (`shared/host-api/`, validated examples).
- [x] **Design docs** - agent runtime and stall protection (`docs/12`), tool catalog (`docs/13`), system prompt + shipped prompt texts (`docs/14`, `src/ai/prompts/`), UI shell with wireframes and keymap (`docs/15`), profiler + ITP trace protocol (`docs/16`), distribution (`docs/21`), predecessor parity/surpass/migration (`docs/20`), Nuke and toolchains (`docs/23`), testing (`docs/19`), roadmap M0-M11 (`docs/18`).
- [x] **C4 readiness gap analysis + C4-side work items** - `docs/17` (Bedrock Net/Process/FS.Watch/Time/Text, suspected CBind bugs S1-S7, C4LSP L1-L9, Nuke N1-N9 - N1/N3/N4/N6 done, the old editor plugin -> declarative `c4` plugin, Bedrock `Profile::`).
- [x] **M0 item 2: theme tokens + static mocks** - `frontend/css/theme.css` (every `docs/07` token; Visual Assist X syntax default + Grayscale), `frontend/mocks/` (token board + the six `docs/15` review screens, phone layouts included), 25 screenshots at 1920x1080 / 1366x768 / 390x844 in `frontend/mocks/screenshots/`, reviewed by eye. `frontend/mocks/build.py` regenerates icons, lints (ASCII, tabs, no raw colors outside `theme.css`) and retakes screenshots with Playwright. **Awaiting owner sign-off** (questions in `frontend/mocks/index.html`).
- [x] **GPU debugger designed** - `docs/22` (PIX/RenderDoc/Nsight comparison, architecture with swappable replay backends, RenderDoc-first then own "Flashbang" recommendation, views, capture flow, AI, phases), `docs/features/17` (F-GPU), US-GPU-1..9 + J15, T-GPU tools, M12/M13, Bedrock `Gpu::` (P-8). Mocks 07-09 in PIX's layout with screenshots at three sizes. **Owner:** O11 (backend) in `docs/99`.
- [ ] **M1 in progress: the C4 server (`src/`, `ied-server`, and the `ied` CLI in `cli/`)** (2026-10-20). `nuke conformance` builds both and runs the protocol suite (51/51), the schema cross-check and the server checks (79); `nuke e2e` drives the web client against the mock and this server; `nuke test ied-server ied` runs the unit tests. See `src/README.md`. Done:
  - protocol v1: frames, topics with rings and snapshots, resume, backpressure with coalescing and gaps, opId idempotency, cancellation, documents with lease and rebase, `fs.*` over real folders, presence, blobs, static files, Host/Origin checks;
  - auth (`docs/11` "Authentication on the wire"): the launch token on the `OPEN` line, a session cookie per port, device sessions kept as digests, the Argon2id owner password (Bedrock `Crypto::`, B66), the login throttle, `auth.*` with revoke, LAN bind refused without a password, the audit log, `frontend/login.html` checked by screenshot at desktop and phone width;
  - file watching (`docs/10` "Files changed on disk"): batches folded per path, the server's own writes recognized by digest, outside writes moving revisions and reaching open documents;
  - single instance and the `ied` CLI over the control pipe (`docs/10` "Local control channel", Bedrock `Net::LocalListener`, B67);
  - the plugin host for built-in, app and project-local declarative plugins (`docs/02` "The host as built"): manifests validated by Bedrock's JSON Schema validator (B68), app plugins approved by content digest, local ones behind workspace trust, precedence, enable/disable, hot reload;
  - workspace trust in app data (folders open untrusted);
  - settings tiers (`docs/07` "Settings on the wire"): plugin-declared settings, app / solution / project tier files live and checked with problems on their lines, merge kinds, comment-keeping revision-checked writes;
  - solutions and projects (`docs/03` "The server as built");
  - the IED output source: the server's log as every workspace's `out:<wsId>:ied` (plugin and solution problems show there);
  - layouts (`docs/10` "layout:<wsId>:<deviceClass>"): the client's layout document per workspace and device class, revision-checked, a solution's kept in `.ied/local/layout/`, a folder's in the app data;
  - the web client shell (M0 item 4, `docs/15` "The web client as built", 2026-10-20): the protocol client with resume, commands, keybindings with chords and when-clauses, VS menus and context menus, the command palette, docking (drag to groups and edges, splitters) kept on the server, the status bar, notifications, banners, the phone shell; plugins' UI modules (`docs/02` "UI modules") with the built-in `editor` plugin's Solution Explorer, Output and read-only documents. It runs unchanged on the mock and this server: `nuke e2e` (56 unit tests; 15 scenarios across 1920x1080, 1366x768 and 390x844 on both servers, resume after a restart and after a page closed mid-stream included). Bugs it found: BUG-292 (a second WebSocket close frame), local plugins' files not served (`/w/<wsId>/plugins/`), the shell left without a dock host when its plugins were disabled (the Home dock host).

  Next in M1: the 24 h idle soak (`tools/soak/idle_soak.py`, running since 2026-10-20 21:20). Then M2.
- [ ] **M2 in progress: the editor core** (2026-10-20). Done:
  - Monaco on the server's documents (`plugins/editor/ui/editorview.js`, `shared/ui-kit/docsync.js`, `docs/10` "The client side as built"): one edit in flight, acknowledged by its event; other writers' edits moved over the pending ones with the mirror of the server's rule (a seeded random interleaving test found and pinned a bug in it); resync from a snapshot when an edit cannot be placed; the lease with Edit Here; unsaved dots; Save / Save All; the editor status item; the theme from the tokens; Monaco served from the install (`/vendor/<library>/`), a read-only view when it is not installed;
  - document languages from plugins: the `languages` contributions decide (the server's hard-coded extension map is gone), the built-in declarative `plugins/languages` covers common languages, `ied.contributions(point)` lets UI modules read contribution points;
  - e2e: edit and save, two windows sharing a document through the lease, a server (AI) edit landing between keystrokes - on the mock and the real server;
  - the language server pool (`src/lsp/`, `docs/24`): profiles from plugins' `lspProfiles` chosen by selector and `when` (`src/plugins/when.cf`, the client's grammar on the server), lazy start (staged copies for `launchMode: staged`), initialize with the encodings the profile allows, a writer thread per server so a stuck server never holds the lock, documents from the store (`didOpen` / full-text `didChange` / `didSave`), positions and URIs rewritten both ways (UTF-16 for clients, UTF-8 or bytes for servers; `ied://` for the workspace's files), diagnostics on `diag:<wsId>`, states and refreshes on `lsp:<wsId>`, `lsp.request` / `lsp.capabilities` / `lsp.status` / `lsp.restart`, restarts with backoff, shutdown with IED. 46 server unit tests; `tools/server-checks/tests/test_language_servers.py` drives it with `fake_lsp.py` (85 server checks).

  - the language features in Monaco (`plugins/editor/ui/language.js`, `lspconvert.js`): one set of providers per language and server from `lsp.capabilities` - hover, completion with resolve, signature help, the go-to family and references with peeks into other files, highlights, symbols, folding, semantic tokens, inlay hints, formatting, rename, code actions; diagnostics as markers and in the new **Error List** tool window (`docs/15` "The editor plugin");
  - C4 end to end: the declarative `c4` plugin lives in the C4 tree (`C4/IedPlugin/`) and ships in every toolchain as `ied/`; project-scoped language servers (`docs/24` "Scope": one C4LSP per project, with the options Nuke writes for it); `nuke init --ied` writes a workspace's solution, project files and the plugin pin (`Nuke/README.md` "IED solution"), and this repository has its `IED.ied.sln`. Verified live on this repository: C4LSP's semantic colors, hover, cross-file go to definition, diagnostics in the editor and the Error List. Running it found BUG-295..298 in C4LSP (completion triggers, member completion, tokens crossing lines), fixed with tests;
  - Solution Explorer file operations: New File / New Folder named in place, Rename (F2) and Delete (Del, confirmed) for files and folders; the server's `fs.rename` / `fs.delete` take folders, and open documents follow renames (`doc.renamed`, their language servers get `didClose` / `didOpen`). e2e `explorer_file_operations`, `renamed_document_follows`; server checks `test_file_operations.py`;
  - the recoverable trash (F-OPS-13, `docs/10` "Files"): `fs.delete` moves files and folders to `<data>/trash/`, `trash.list` / `trash.restore` / `trash.purge`, a 4 GB cap; the Explorer offers Undo after a delete (`ied.toast` with an action);
  - fixed on the way: the Explorer collapsed on any file change on disk (every `ws:` event notified the workspace listeners, and a tree refresh lost nested folders); a killed soak run left its server running (the soak now binds its server to itself with a job object); the Error List showed `null` codes.

  Next in M2: find in files (`fs.search` on the server - the mock has it), navigation back / forward, Go To All, header / source switching, the track-changes margin; C4LSP L1 (UTF-8 positions) so the `c4` profile can drop `bytes`.
- [x] **C4 readiness (`docs/17`) is green except the 24 h scale-test soak (item 8)** (2026-10-20): networking with an event loop, processes with ConPTY, file watching, UTF-16, base64/SHA-1, regex, reflection serialization, CBind fixes, incremental builds (`--incremental`, Nuke's Debug/Development: a one-module edit of a 60k-line program rebuilds in 2.6 s), streaming JSON (`JsonStream` for JSON-RPC as it arrives) and custom-allocator pools. M1 (server skeleton) can start.
- [x] **M0 items 3 + 5: test doubles** - `tools/mock-server/` (protocol v1 + fixtures: workspace, docs with lease/rebase, scripted chat with tools and approvals, ANSI task runs, echo terminal, layouts, presence; debug console at `/_mock/`), `tools/conformance/` (46 black-box tests, green against the mock; needs `testhooks.*` on the real server), `tools/fake-model/` (40 scripts, every `docs/12` stop class + must-not-stop cases, discovery endpoints), `tools/schema-check/` (examples valid). All stdlib; `python -I tools/selftest.py` runs everything (~40 s, green on Python 3.12 and 3.14). Resume verified in a real browser against the mock.

## Next actions

**Owner:** review `docs/99` "Open - owner's call" (O1-O3, O5-O11) and the M0 deliverables below; sign off the schemas so they can be frozen; sign off the look on `frontend/mocks/index.html` (sign-off questions at the bottom, including the GPU debugger).

**M0 - can start now (no compiler needed), in this order:**

1. Freeze `shared/host-api` v1 after owner review (manifest, solution, project, frames).
2. ~~`frontend/css/theme.css` + static HTML mocks of the six review screens (`docs/15`); screenshots at 1920x1080, 1366x768, 390x844~~ - done; owner sign-off pending. `frontend/mocks/css/shell.css` and `code.css` are the seed for `frontend/css/layout.css` in item 4.
3. `tools/mock-server/` (Python, protocol v1) + `tools/conformance/` suite.
   - **Done** (see `tools/README.md`). The frontend shell (item 4) runs on it: `python -I tools/mock-server/mock_server.py --static frontend`.
4. ~~Frontend shell on the mock server: sidebar, docking, menus/commands/keybindings, status bar, WS client with resume, phone shell - with Playwright e2e and resume tests~~ - done, on the real server too (`nuke e2e`).
5. `tools/fake-model/` + scripted cases for every stop class in `docs/12`.
   - **Done**: `tools/fake-model/README.md` maps each stop class to its scripts; `probe.py` replays any script (or probes a real server).
6. SearXNG-on-Windows spike (`docs/21`).

**Follow-ups from the test doubles (owner / next protocol edit):**

- Fold the 12 "Protocol decisions" in `tools/mock-server/README.md` into `docs/10` (or change them; the mock and conformance suite follow whatever is decided) and add the `testhooks.*` surface to `docs/10` or `docs/19`.
- `shared/host-api/README.md` says the schemas use only type/required/properties/...; they also use `minimum`, `maximum`, `maxItems`, `uniqueItems` (the validator supports all of them and rejects unknown keywords).

**Follow-ups from the shell (owner / later milestones):**

- Playwright for the bundled Python: `setup.bat` could install it (a pip download plus Chromium) so `nuke e2e` needs nothing on PATH; today it uses a Python on PATH that has it.
- The terminal dock and the chat activity appear with their plugins (M3, M4); the explorer's file operations, Monaco and LSP with M2. Tree expansion and the Output source are per-tab UI state, not kept on the server yet.

**C4 (`C4/`, in parallel):** verify suspected bugs S1-S7 and file/fix confirmed ones per `AGENTS.md` (`C4/WORK.md` is the compiler's own tracker); Bedrock P-1 modules; scale test; Nuke N2 (`nuke init --ied`), N5 (diagnostics JSON, needs the compiler), N7; the declarative `c4` plugin (schema-valid today: see `shared/host-api/examples/c4-plugin/`); C4LSP L1-L5.

**Follow-ups from the move (owner):**

- Retake the mock screenshots (`python -I frontend/mocks/build.py shots`): most still show the old sample names.
- Decide a LICENSE before making the repository public (none is committed; without one it is all rights reserved).
- The clang 22.1.0 build crashes intermittently on this machine under the parallel build (access violations in clang/libclang, a different file each time, never reproducible); Frost retries crashed compiles twice. If it persists, check the machine's memory stability (a DDR5 EXPO profile is a common cause) or try a newer LLVM.
- **Blue screens (2026-10-20):** four memory-manager bugchecks in five days - 0x12B FAULTY_HARDWARE_CORRUPTED_PAGE (10-16), 0xBE (10-19), 0x1A/0x41790 page table page corrupted (10-20 09:37) and 0x1A/0x61948 (10-20 ~10:50). The RAM runs at JEDEC 4800 MT/s (EXPO is off), so this is not a memory overclock. Both 10-20 crashes happened during an AddressSanitizer build of the compiler (Frost `-asan`: instrumented tools reserve and fault in huge shadow ranges); ordinary builds, gates and test batches never crashed. A user-mode program cannot legitimately bugcheck Windows, so the root cause is hardware, firmware or a driver, and ASan is the trigger: **do not run `-asan` builds on this machine.** Owner: update the BIOS (F31 is from 2024-08), run MemTest86 overnight, check PBO / Curve Optimizer offsets; install WinDbg to read `C:\Windows\MEMORY.DMP` (`!analyze -v`) for the process and stack of the next one. Builds and tests keep their parallelism moderate (`FROST_JOBS`, DifferentialTest `--jobs`). **Diagnosed later that day: the RAM is failing.** After the 10:54 reboot every Claude Code agent process died exactly 606 s after starting (0xC0000409); the cached copy of its `claude.exe` failed its signature check while an unbuffered copy from disk was valid, and the two differed in 50 single-bit flips in a regular pattern (same offsets in pages 16 KiB apart) - a failing DRAM signature. `git fsck` was clean. Owner: find the bad DIMM (MemTest86, each stick alone) and replace it; a reboot clears a corrupted file cache in the meantime.
- Old local checkouts of C4 can be deleted once this repository is pushed; game projects move their `Project.py` to `nuke.toml` and pin a toolchain (`docs/23`).

**Then M1-M11** per `docs/18`: the readiness checklist in `docs/17` is green but for the 24 h soak (`python C4/assets/ScaleTest/run.py --seconds 86400 --levels O2`).

## Decisions log

| Date | Decision | Where recorded |
|---|---|---|
| 2026-10-19 | JS plugin runtime = **QuickJS-ng v0.17.0** (ChakraCore dropped: unmaintained, dead links); behind a C shim | `docs/99` #6, `docs/02` |
| 2026-10-19 | ~~Third-party deps pre-vendored to `third_party/` (gitignored)~~ superseded 2026-10-20: git sources are submodules in `third_party/`, the rest are pinned downloads in `tools/setup/deps.toml` installed by `setup.bat` into `vendor/` | `tools/README.md` |
| 2026-10-19 | UI delivery = split server + served web frontend; WebView2 wrapper optional later | `docs/99` #1, `docs/01` |
| 2026-10-19 | Tracy is a feature reference only - never linked | `docs/05`, `docs/16` |
| 2026-10-19 | **Predecessor parity is the floor; IED must drastically surpass it** (owner) - orchestration area, differentiators raised to P1 | `docs/09`, `docs/20`, `docs/features/16` |
| 2026-10-19 | Plugin runtimes: builtin (compiled in) / declarative / JS / process - C4 has no DLL output | `docs/02`, `docs/17` |
| 2026-10-19 | Protocol v1: one WS per tab, topic seq + snapshots + gaps, opId idempotency, doc lease + rebase | `docs/10` |
| 2026-10-19 | `tools/` test doubles are **stdlib-only Python** (own RFC 6455 framing in `tools/pylib/ws.py`): `python -I` ignores user site-packages, so pip deps would silently vanish | `tools/README.md` |
| 2026-10-19 | Conformance needs a **`testhooks.*` method surface** (topics with tiny rings, emit/stream, delay, counter, restart, scratch file, server edit); the real server exposes it only behind a test flag | `tools/conformance/README.md` |
| 2026-10-19 | Mock-pinned protocol details (proposed for `docs/10`): sub result shape, replay only when it fits the client budget, opId rules, UTF-16 columns, liveness counts pongs, seq continues across restart | `tools/mock-server/README.md` |
| 2026-10-19 | Security: loopback default, Host/Origin checks, loopback one-time token, password required for LAN | `docs/11` |
| 2026-10-19 | Task variables use `${name}` (replaces `{name}`) | `docs/06`, schemas |
| 2026-10-19 | Git backend = git CLI; libgit2 kept vendored for later | `docs/99` #14 |
| 2026-10-19 | Claude: API-key provider + bridge driving the owner's own `claude` CLI; IED never handles Claude credentials (Anthropic policy) | `docs/04`, `docs/99` O2 |
| 2026-10-19 | Profiler trace protocol = ITP v1 (explicit interning, timestamped thread renames, task events) | `docs/16` |
| 2026-10-19 | SearXNG bundled via embeddable Python (spike pending); tor bundled; Edge via CDP for browser tools | `docs/21` |
| 2026-10-19 | Native APIs only through hand-written C shims with opaque handles until CBind struct bugs are verified/fixed | `docs/17` |
| 2026-10-19 | **Code uses the Visual Assist X palette** (owner; the old C4 editor plugin's `c4-visual-assist-dark` values); Grayscale syntax palette is a per-user option; the desaturated "Muted" proposal is dropped | `docs/07`, `docs/99` #16 |
| 2026-10-19 | **GPU debugger follows PIX's layout and styling** (owner); RenderDoc is a backend (C++ replay API, no Python) and a capability reference, never the look | `docs/22`, `docs/99` #17 |
| 2026-10-19 | Mock icons: only referenced codicons, embedded as data-URI masks by `frontend/mocks/build.py` (no font build, works from disk); primary buttons are inverted grayscale, status bar states are tints | `frontend/mocks/README.md`, `docs/07` |
| 2026-10-20 | **C4 lives in this repository** (owner): one versioned tree for the IDE, the language and the build system, so an agent can fix compiler or IDE bugs and continue; old history dropped; Frost folded in | `README.md`, `AGENTS.md` |
| 2026-10-20 | **Nuke rebuilt**: TOML manifests + optional Python, phases/steps/commands, chained commands, one shared copy, versioned toolchain store with `dev` | `docs/23`, `Nuke/README.md` |
| 2026-10-20 | Nothing large is committed: `setup.bat` installs Python, LLVM and web libraries from pinned, checksummed URLs; vendored git sources are submodules | `tools/README.md` |
