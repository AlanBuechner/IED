# 99 — Open questions (blocking first)

Decide these during the design phase, then **move the answer into the relevant doc and mark it resolved here** (keep a one-line record so history is visible). Items marked *blocking* gate implementation.

## Blocking

1. **Webview choice.** WebView2 (Windows-first, lighter integration on the target OS) vs embedded Chromium (cross-platform). The plan targets Windows Visual-Studio-style UX; cross-platform IED binaries are not required. → `docs/01` NOTE: backend will have a served front end like a normal server so we can access the ui remotely(will also make mobile support easier) but a bundled desktop app for a more seamless experience i will let you pick the best tool for the job.
2. **IPC protocol v1.** Framing/serialization for webview↔host; whether LSP/DAP/token-stream traffic gets a dedicated high-throughput side channel or rides the same JSON loop. Must support: request/response, push events, streaming chunks, cancellation. → `docs/01`
3. **Plugin manifest v1 final schema.** The sketch in `docs/02` is the starting point; freeze field names and permission list before any plugin code exists — every default plugin codes against it. → `shared/host-api/`, `docs/02`
4. **Project/solution file schemas.** Freeze `.ied.sln` / `.ied.proj` (sketch in `docs/03`) including the toolbar command-template grammar (`{var}` substitution, `onlyIf`). These are on-disk formats — expensive to change later. → `shared/host-api/`, `docs/03`, `docs/06`
5. **C4 compiler readiness.** The compiler must be able to build this repo before any implementation starts (stated precondition). Confirm the target C4 version/features needed (e.g. does it support what the host needs for webview embedding, sockets, process spawning) and file gaps upstream in `C:\GameDev\C4`. → `WORK.md`

## Design-phase (resolve before implementing the affected area)

6. **JS plugin runtime.** **Resolved: QuickJS-ng** (vendored at `third_party/quickjs-ng-v0.17.0`, amalgam single-file build). ChakraCore is out — unmaintained since 2020 and its official binary links are dead. Remaining work when the first JS plugin lands: wire async I/O to host APIs + timers around the amalgam. → `docs/02` NOTE: i will let you pick whatever is the best for the job
7. **Context compression strategy details.** Thresholds, which model role summarizes, what the compressed ledger entry format looks like. Prototype against real long sessions before polishing the UI panel. → `docs/04`
8. **SearXNG packaging.** Bundled binary vs vendored docker image; tor via system install vs bundled. Affects installer size and sandbox plugin's docker assumption. → `docs/04` NOTE: prefer bundled or if not possible auto install isolated. i want the distribution of this to be easy.
9. **Profiler trace format.** The seam between C4's Bedrock profiler hooks and the IED profiler plugin — define the file/socket format early since both sides (two repos) build against it. Must carry: CPU spans, allocations, dynamic thread renames, GPU traces. → `docs/05`, AGENTS.md coordination
10. **`.echo/` migration.** During transition, do C4 repos carry both `.echo/` and `.ied/`? Who writes which? Decide with the Nuke port. → `docs/03` NOTE: .echo will still exist in this folder but c4 will no longer care about echo
11. **Claude Agent SDK integration shape.** How the SDK's own tool loop composes with IED's tool registry (does Claude get IED's tools, or does it stay in its own sandbox?), and what subscription-usage data the SDK/account API actually exposes for the usage views. → `docs/04`
12. **Request routing policy.** Concrete fallback-list semantics per role, load tracking across models, and the privacy flagging rules (what marks a request "sensitive": keyword heuristics? user tagging? solution-level strict mode only?). → `docs/04`

## Later (v2+ candidates, no pressure)

- Out-of-process plugin isolation for misbehaving plugins.
- Per-test structured output from Nuke's TestRunner (Test Explorer UI waits on this).
- Cross-platform IED binaries (only if demand appears; sandbox already covers cross-OS *development*).
- Light theme (plan says no; revisit only if the family ever wants it).
- Plugin marketplace / sharing mechanism (v1: global plugins are manual installs from a path/zip).
