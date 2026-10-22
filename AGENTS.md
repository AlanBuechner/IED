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

1. **The host stays generic.** Nothing C4- or engine-specific may live in `src/`, `frontend/`, or `shared/`. Language support comes exclusively through plugins, LSP servers and DAP adapters. If you find yourself special-casing a language or engine, stop and make it a plugin contribution.
2. **Server owns all state; the browser is disposable.** IED is a split server + web client (see `docs/01`). No feature may depend on a browser tab staying open: chats, builds, terminals, LSP/DAP sessions live on the server; reconnects resume from the server's ring buffer + disk. The UI must work at phone widths. Don't put authoritative state in the frontend.
3. **Do not reimplement the build system.** IED invokes Nuke (`nuke`, `Nuke/README.md`) for C4 projects. Incremental logic, module graphs, test discovery stay in Nuke. IED's job is: configure, invoke, stream output, surface results. See `docs/06-build-and-nuke-integration.md`.
4. **Mirror the C4/Nuke conventions** (they already exist; don't invent new ones):
   - Numbered docs with a `README.md` index in `docs/`.
   - Build outputs under `build/{bin,int}/{config}-{system}-{arc}/{projName}` (Nuke's layout).
   - Configurations: `Debug`, `Development`, `Dist` — default **Development**. Architecture `x64`, system `windows`.
   - Workspaces are described by `nuke.toml` manifests and pin a C4 toolchain version; nothing vendors Nuke or the compiler.
5. **The `.ied/` hidden folder is committed** (settings + skills must survive deletion of generated files). Per-machine state goes in `.ied/local/` which is gitignored. Generated build artifacts go under `build/` and are gitignored.
6. **Theme discipline:** dark only, grayscale base, no off-grays without meaning. Color is reserved for semantic accents (saved = green, unsaved = yellow, error = red, info = blue). Plugins must use the shared CSS theme tokens, never raw hex values. See `docs/07-settings-and-theming.md`.
7. **Plugins compose on plugins.** New capabilities should be exposed as named bindings/tool calls/skills so other plugins can hook them. If two default plugins need to talk, define a binding rather than a direct dependency.
8. **Includes name files from the project's source folder** (`include "web/rpc.ch"` anywhere in `src/`), or plainly when the file sits beside the includer (`include "session.ch"`); never with `../`. C4 resolves an include beside the includer first, then in the project's source folders (`C4/docs/language/15-projects.md` rule 3).

## Working on C4 and Nuke here

The C4 language (`C4/`) and Nuke (`Nuke/`) live in this repository, so a bug in the compiler, the default library or the build system is fixed here, at the source, never worked around in IED code:

- **C4 bugs:** add the bug to `C4/docs/status/known-bugs.md` (its template, the next `BUG-NNN`), fix it in `C4/` following `C4/AGENTS.md`, add a test that would have caught it, run `nuke build` and `nuke gates` from the repository root (every C4 suite must pass), then delete the entry. `nuke build` refreshes the `dev` toolchain, so workspaces pinned to `dev` (this one included) use the fix immediately.
- **Releasing a toolchain:** bump `C4/C4Lib/src/Compiler/Version.h`, then `nuke install-c4`; installed versions never change.
- **Nuke changes:** `python -m unittest discover -s Nuke/tests` must pass (`test_integration.py` runs against a real installed toolchain); keep `Nuke/README.md` and `docs/23` current.
- **Planned C4-side work for IED** is tracked in `docs/17`: the compiler must build the IED server (it can't yet; that gates everything), C4LSP/C4DAP speak to IED's plugin host, the declarative `c4` plugin replaces the old editor plugin, `nuke init --ied` writes IED solution/project files, and Bedrock gets profiler hooks.
- Setup is `setup.bat` (pins in `tools/setup/deps.toml`); never commit anything it installs (`vendor/`, `toolchains/`).

## What "done" looks like for a feature

- Works end-to-end in the running app, verified by screenshots.
- Errors are surfaced in the UI (output window / status bar), not swallowed.
- Settings are configurable at the right tier (app / solution / project) and documented.
- AI-relevant capabilities are exposed as skills and/or tool calls where useful.
- `WORK.md` updated; related open question resolved in `docs/`.
