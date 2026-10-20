# AGENTS.md — working agreements for IED implementation

This file is the contract for anyone (human or AI) doing implementation work in this repo. Read `IDEplan.md` first, then `docs/`, and keep `WORK.md` updated as you go.

## Process loop

```
design -> implement -> test -> review -> repeat
```

- **Commit at checkpoints.** Descriptive messages; never leave a broken intermediate state on the branch for long.
- **Validate UI with screenshots.** Every meaningful interaction (opening tabs, docking, running builds, chat streaming, context menus) must be screenshot-verified during development — at desktop width *and* phone width (the web client is a first-class target). The UX should look and feel like Visual Studio: dense, clear, no decoration. Also verify the **resume path**: kill the browser mid-stream, reopen, confirm state restored.
- **Test interactions, not just code paths.** A feature is done when a human can use it end-to-end without reading source.
- **Design before implementing** anything that touches the host API, plugin manifest, project/solution formats, or IPC protocol — those are load-bearing and expensive to change later. Record decisions in `docs/` (resolve items from `docs/99-open-questions.md` by editing them into the relevant doc).

## Hard rules

1. **The host stays generic.** Nothing C4- or Claymore-specific may live in `src/`, `frontend/`, or `shared/`. Language support comes exclusively through plugins, LSP servers and DAP adapters. If you find yourself special-casing a language or engine, stop and make it a plugin contribution.
2. **Server owns all state; the browser is disposable.** IED is a split server + web client (see `docs/01`). No feature may depend on a browser tab staying open: chats, builds, terminals, LSP/DAP sessions live on the server; reconnects resume from the server's ring buffer + disk. The UI must work at phone widths. Don't put authoritative state in the frontend.
3. **Do not reimplement the build system.** IED invokes Nuke (`nuke.bat`) for C4 projects. Incremental logic, module graphs, test discovery stay in Nuke. IED's job is: configure, invoke, stream output, surface results. See `docs/06-build-and-nuke-integration.md`.
4. **Mirror the C4/Nuke repo conventions** (this family already has them; don't invent new ones):
   - Numbered docs with a `README.md` index in `docs/`.
   - Build outputs under `build/{bin,int}/{config}-{system}-{arc}/{projName}` (Nuke's layout).
   - Configurations: `Debug`, `Development`, `Dist` — default **Development**. Architecture `x64`, system `windows`.
   - A root launcher (`nuke.bat`) that delegates to the kit folder.
5. **The `.ied/` hidden folder is committed** (settings + skills must survive deletion of generated files). Per-machine state goes in `.ied/local/` which is gitignored. Generated build artifacts go under `build/` and are gitignored.
6. **Theme discipline:** dark only, grayscale base, no off-grays without meaning. Color is reserved for semantic accents (saved = green, unsaved = yellow, error = red, info = blue). Plugins must use the shared CSS theme tokens, never raw hex values. See `docs/07-settings-and-theming.md`.
7. **Plugins compose on plugins.** New capabilities should be exposed as named bindings/tool calls/skills so other plugins can hook them. If two default plugins need to talk, define a binding rather than a direct dependency.

## Cross-repo coordination

IED is not built in isolation. When implementation reaches these points, the C4 repo (`C:\GameDev\C4`) needs matching work:

- **C4 compiler** — must be able to build this repo (it can't yet; that gates everything).
- **C4LSP / C4DAP** — updated to speak to IED's plugin host instead of Echo.
- **Nuke** — `EchoConfig.py`'s `.echo/` bridge becomes an `.ied/` bridge: `nuke --init` should write the IED project/solution files and LSP options file, and every build should keep the LSP options in sync (same pattern as today's `.echo/c4-lsp.json`).
- **C4Assist** — currently Go code written for Echo; rewrite it to target IED.
- **Bedrock / C4 default library** — needs profiler hooks so C4 apps can use the IED profiler out of the box.

If you find a bug in the C4 compiler or default library document it in the knows-bugs.md file in C4 then resolve the issue validate that it is resolved then remove the bug listed.

## What "done" looks like for a feature

- Works end-to-end in the running app, verified by screenshots.
- Errors are surfaced in the UI (output window / status bar), not swallowed.
- Settings are configurable at the right tier (app / solution / project) and documented.
- AI-relevant capabilities are exposed as skills and/or tool calls where useful.
- `WORK.md` updated; related open question resolved in `docs/`.
