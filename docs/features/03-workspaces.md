# Features: workspaces (WSP)

Design: `docs/03` (project and solution model), schemas in `shared/host-api/schemas/`.

A **workspace** is what one browser tab views. It is one of:

| Kind | Opened from | Has `.ied/`? | Notes |
|---|---|---|---|
| **Solution** | `*.ied.sln` | yes (at the solution root) | Full feature set. |
| **Folder** | `ied ./`, *Open Folder*, Explorer right-click | created lazily on the first workspace-tier write (setting, skill, trust record stays in app data) | No projects: tree = the folder; toolbar from `.ied/folder.json` if present. |
| **File** | `ied file.txt`, Explorer right-click on a file | no | Opens the file's folder as a hidden lightweight folder workspace, with only that document open and the tree collapsed. |
| **Federation** | `*.ied.fed` or *Add root to workspace* | each member keeps its own `.ied/` | Several solutions/folders in one workspace (see below). |

## Solution and project model

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-WSP-1 | `.ied.sln` / `.ied.proj` JSON files (schema v1); project `root` may point outside the project file's folder | P0 | US-WSP-1 | src/host, shared/host-api |
| F-WSP-2 | Solution groups (display folders in the tree, VS "solution folders") | P0 | US-WSP-6 | src/host, plugins/editor |
| F-WSP-3 | Startup project selection (per device? no - per workspace, stored in `.ied/local/`) and VS "multiple startup projects" P2 | P1 | US-WSP-9 | plugins/editor |
| F-WSP-4 | File-system watching of solution/project files: external regeneration (e.g. `nuke --init`) reloads the tree without reopening | P0 | US-WSP-1 | src/fs |
| F-WSP-5 | Project file include/exclude globs (default: everything under `root` except `.git`, build output dirs from settings) | P0 | US-WSP-6 | src/host |
| F-WSP-6 | Validation errors in solution/project files are shown in the Error List with file/line, never silently ignored | P0 | - | src/host |
| F-WSP-7 | Project properties page rendered from the project-file schema (+ plugin-contributed property pages) | P2 | US-WSP-12 | plugins/editor |
| F-WSP-8 | *Miscellaneous Files* node for documents opened that belong to no project (VS parity) | P1 | US-WSP-11 | plugins/editor |
| F-WSP-9 | Multiple solutions in one folder: picker on open; they share the folder's single `.ied/` (skills, dictionary), and solution-tier settings are per solution (`.ied/solutions/<Name>.json`) | P1 | - | src/host |

## Opening things

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-WSP-10 | Open folder (no project) | P0 | US-WSP-3 | src/host |
| F-WSP-11 | Open single file (lightweight workspace) | P0 | US-WSP-3 | src/host |
| F-WSP-12 | `ied <path>` resolves: `.ied.sln` -> solution; folder containing exactly one `.ied.sln` -> asks "open solution or folder?" (remembered); else folder; file -> file mode, unless the file is inside an open workspace (then it opens there) | P0 | US-WSP-3 | src/host |
| F-WSP-13 | Explorer integration: *Open with IED* on files, folders and folder backgrounds; `.ied.sln` file association (installer) | P1 | US-WSP-4 | installer |
| F-WSP-14 | Recent list (app tier), pinning, removing missing entries | P1 | US-SHL-10 | src/host |
| F-WSP-15 | Clone repository (URL -> folder -> open), using the git plugin | P1 | US-SHL-10 | plugins/git |

## Workspace switcher (top of sidebar)

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-WSP-20 | Switcher shows open workspaces with live badges (running chats/builds, errors), recent ones below, then "Open...", "New project...", "New federation..." | P0 | US-SHL-8 | frontend |
| F-WSP-21 | Switching keeps the other workspace running on the server; per-workspace layout is restored | P0 | US-SRV-7 | frontend, src/host |
| F-WSP-22 | Close workspace: stops its LSPs/terminals after confirming running work; chats persist | P0 | - | src/host |

## Federations

Interpretation of the plan's "federated projects" (confirm in `docs/99`): a named set of roots that are worked on together - e.g. C4 + Claymore + IED - because the family's work constantly crosses repo boundaries (a Claymore bug is a C4 compiler fix).

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-WSP-30 | `.ied.fed` file (or app-tier record): list of member roots (solutions or folders), display names, which member is "primary" | P1 | US-WSP-5 | src/host |
| F-WSP-31 | Tree shows one top node per member; search scopes: current project / current member / whole federation | P1 | US-WSP-5, US-EDT-4 | plugins/editor |
| F-WSP-32 | Each member keeps its own `.ied/`, plugins, LSP options and trust; settings resolve per member for files inside it | P1 | US-WSP-5 | src/host |
| F-WSP-33 | Chats in a federation see all members' `AGENTS.md` (each scoped to its root) and can use tools across members, subject to each member's trust and egress policy (strictest wins for a chat that touched several) | P1 | US-WSP-5, US-CTX-5 | src/ai |
| F-WSP-34 | Federation-level toolbar (e.g. "build C4 compiler, refresh Claymore's vendored C4.exe, build Claymore") composed from member toolbars | P2 | US-WSP-5 | plugins/editor |

## Trust

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-WSP-40 | Trust records in app tier (path + hash of `.ied.sln` local-plugin section); trusting a parent folder trusts children (VS Code model) | P0 | US-WSP-7 | src/host |
| F-WSP-41 | Untrusted mode disables: project-local plugins, project toolbar commands, AI tools that execute code (shell, build, run, debug), MCP servers defined by the project, LSP profiles that run project-provided binaries | P0 | US-WSP-7 | src/host |
| F-WSP-42 | Re-prompt when the local-plugin section or toolbar commands change after a pull (diff shown) | P1 | US-WSP-7 | src/host |

## Solution Explorer (tree)

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-WSP-50 | Tree: solution -> groups -> projects -> folders -> files; distinct file-type icons (icon theme in ui-kit + plugin-contributed icons for their file types) | P0 | US-WSP-6 | plugins/editor, shared/ui-kit |
| F-WSP-51 | Decorations: git status (modified/added/untracked/conflict), error/warning counts rolled up to folders, dirty-buffer marker | P0 | US-WSP-6 | plugins/editor |
| F-WSP-52 | File operations: new file/folder, rename (F2), delete -> trash, drag-drop move/copy, cut/copy/paste, copy path / relative path, reveal in Explorer, open in terminal | P0 | US-WSP-10 | plugins/editor, src/fs |
| F-WSP-53 | Rename/move triggers LSP `workspace/willRenameFiles` refactors when the server supports it, with preview | P1 | US-WSP-10 | plugins/editor |
| F-WSP-54 | Tree filter box (search), "track active document" (sync), collapse all, show all files vs project-only view | P0 | US-WSP-6 | plugins/editor |
| F-WSP-55 | File nesting rules (contributed: e.g. `.ch` under `.cf`, `.meta.c4` under its asset) | P2 | - | plugins/editor |
| F-WSP-56 | Multi-select operations; keyboard navigation (type-ahead find) | P1 | US-WSP-10 | shared/ui-kit |

## Project templates

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-WSP-60 | *New Project / New Solution* dialog listing `projectTemplates` contributions (name, description, icon, parameters schema) | P1 | US-WSP-8 | plugins/editor |
| F-WSP-61 | Template execution = a command list (like toolbar steps), typically calling the build system's init (`nuke --init`) and then adding the result to the solution | P1 | US-WSP-8 | plugins/editor |
| F-WSP-62 | *Add New Item* (file templates per language, contributed) | P2 | US-WSP-8 | plugins/editor |
