# 99 — Open questions (blocking first)

Decide these during the design phase, then **move the answer into the relevant doc and mark it resolved here** (keep a one-line record so history is visible). Items marked *blocking* gate implementation.

## Blocking

1. **Webview choice.** WebView2 (Windows-first, lighter integration on the target OS) vs embedded Chromium (cross-platform). The plan targets Windows Visual-Studio-style UX; cross-platform IED binaries are not required. → `docs/01`
2. **IPC protocol v1.** Framing/serialization for webview↔host; whether LSP/DAP/token-stream traffic gets a dedicated high-throughput side channel or rides the same JSON loop. Must support: request/response, push events, streaming chunks, cancellation. → `docs/01`
3. **Plugin manifest v1 final schema.** The sketch in `docs/02` is the starting point; freeze field names and permission list before any plugin code exists — every default plugin codes against it. → `shared/host-api/`, `docs/02`
4. **Project/solution file schemas.** Freeze `.ied.sln` / `.ied.proj` (sketch in `docs/03`) including the toolbar command-template grammar (`{var}` substitution, `onlyIf`). These are on-disk formats — expensive to change later. → `shared/host-api/`, `docs/03`, `docs/06`
5. **C4 compiler readiness.** The compiler must be able to build this repo before any implementation starts (stated precondition). Confirm the target C4 version/features needed (e.g. does it support what the host needs for webview embedding, sockets, process spawning) and file gaps upstream in `C:\GameDev\C4`. → `WORK.md`

## Design-phase (resolve before implementing the affected area)

6. **JS plugin runtime.** QuickJS vs ChakraCore (or similar). Needs: async I/O to host APIs, timers, enough of a DOM-free environment for UI-logic plugins. Low risk either way; decide with the first JS plugin. → `docs/02`
7. **Context compression strategy details.** Thresholds, which model role summarizes, what the compressed ledger entry format looks like. Prototype against real long sessions before polishing the UI panel. → `docs/04`
8. **SearXNG packaging.** Bundled binary vs vendored docker image; tor via system install vs bundled. Affects installer size and sandbox plugin's docker assumption. → `docs/04`
9. **Profiler trace format.** The seam between C4's Bedrock profiler hooks and the IED profiler plugin — define the file/socket format early since both sides (two repos) build against it. Must carry: CPU spans, allocations, dynamic thread renames, GPU traces. → `docs/05`, AGENTS.md coordination
10. **`.echo/` migration.** During transition, do C4 repos carry both `.echo/` and `.ied/`? Who writes which? Decide with the Nuke port. → `docs/03`

## Later (v2+ candidates, no pressure)

- Out-of-process plugin isolation for misbehaving plugins.
- Per-test structured output from Nuke's TestRunner (Test Explorer UI waits on this).
- Cross-platform IED binaries (only if demand appears; sandbox already covers cross-OS *development*).
- Light theme (plan says no; revisit only if the family ever wants it).
- Plugin marketplace / sharing mechanism (v1: global plugins are manual installs from a path/zip).
