# WORK.md — current state and next actions

Living document. Update it at every checkpoint commit.

## Current state (scaffold only — no code)

- [x] `IDEplan.md` — vision document (pre-existing).
- [x] Folder structure created: `docs/`, `Nuke/`, `src/`, `frontend/`, `shared/{host-api,ui-kit}`, `plugins/{chat,editor,git,terminal,sandbox,profiler}`, `.ied/`.
- [x] High-level planning docs in `docs/` (01–08) plus open questions (`99`).
- [x] Root conventions: `README.md`, `AGENTS.md`, `.gitignore`.
- [x] Third-party dependencies pre-downloaded into `third_party/` (gitignored): monaco 0.57.0, xterm 6.0.0 (+addons), codicons v0.0.46-45, quickjs-ng v0.17.0 amalgam, WebView2 1.0.4258.31, SearXNG master, Tracy (wolfpld), libgit2 master. Reproduce: `tools/fetch-deps.ps1`; committed record: `tools/deps-manifest.json`. ChakraCore dropped (unmaintained + dead download links).
- [ ] No code. The C4 compiler cannot build this repo yet.

## Next actions (in dependency order)

1. **Resolve the blocking open questions** in `docs/99-open-questions.md`: webview choice, IPC protocol shape, plugin manifest v1, project/solution file formats. These gate everything else.
2. **Define the host API concretely** (`shared/host-api/`): manifest types, contribution interfaces (sidebar icons, tabs, toolbar, output tabs, skills, tool calls, LSP profiles, DAP providers, settings schema, system-message sections), binding registry. This is the contract every plugin codes against — get it right before any plugin exists.
3. **Theme tokens** (`frontend/css/`): the grayscale palette + semantic accent tokens as CSS custom properties. Cheap to do now, and everything visual depends on it.
4. **Port the Nuke kit** into `Nuke/` (from `C:\GameDev\C4\Nuke`) once the C4 compiler can build this repo: root launcher, `Config.py` with IED's projects, project manager, incremental builder, test runner. See `docs/06`.
5. **Host skeleton** (`src/`): app lifecycle, webview embed + IPC loop, settings loader (app/solution/project tiers), plugin discovery/validation/enabling.
6. **Implement default plugins in this order** (each unlocks the next's usability):
   1. `editor` — core UX: Monaco, project tree, output window, toolbar; LSP/DAP wiring.
   2. `chat` — core value: streaming chat, tool calls, model groups, context panel.
   3. `terminal` — bottom shell (also the AI's shell tool).
   4. `git` — source control UI.
   5. `sandbox` — docker cross-OS builds.
   6. `profiler` — Tracy-like view + C4 library hooks (needs C4 repo work).
7. **C4 repo coordination** (parallel track, see AGENTS.md): LSP/DAP retarget from Echo to IED, Nuke `.ied/` bridge, C4Assist rewrite, Bedrock profiler hooks.

## Decisions log

| Date | Decision | Where recorded |
|---|---|---|
| 2026-10-19 | JS plugin runtime = **QuickJS-ng v0.17.0** (ChakraCore dropped: unmaintained since 2020, official binary links dead) | `docs/99` #6, `tools/README.md` |
| 2026-10-19 | Third-party deps pre-vendored to `third_party/` (gitignored); committed SHA256 record in `tools/deps-manifest.json`; SearXNG via codeload tarball (Windows-illegal filenames break git clone) | `tools/README.md` |
