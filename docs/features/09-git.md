# Features: git (GIT)

Owner: `plugins/git` (UI + orchestration) on `src/git` (host plumbing).

**Backend decision (recommended, see `docs/99` #14):** the **git CLI** is the backend for everything that mutates or touches the network (commit, merge, rebase, fetch/push, LFS, hooks, credential helpers) - so behavior matches the terminal exactly and Git Credential Manager, LFS and hooks just work. Read-heavy views (status, log graph, diff, blame) also start on the CLI with machine-readable formats (`--porcelain=v2 -z`, `log --format` with NUL separators); libgit2 stays vendored as a later optimization for the graph if profiling demands it. A custom source-control provider interface (`sourceControlProviders`) keeps room for Perforce/Fossil plugins.

**Status (2026-10-21, `docs/05` "git" As built):** built - F-GIT-1 (the folder's repository, nested ones, submodules found; a picker when several), F-GIT-2, F-GIT-3 (no "jump to HEAD" button yet: a ref's click reveals its commit; search by message, author, email or hash, not path), F-GIT-4, F-GIT-10 (flat lists), F-GIT-11, F-GIT-12 (summary length and amend; no templates or recent messages), F-GIT-20 (force-with-lease and pull modes on the wire; the UI pushes plainly or sets the upstream), F-GIT-21, F-GIT-24 (create with untracked, apply, pop, drop; no stash diff view). The rest is open.

## Repository and overview

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-GIT-1 | Repo discovery across workspace roots, nested repos, submodules; repo picker when several | P0 | US-GIT-1 |
| F-GIT-2 | Left pane (Fork-style): local branches (with ahead/behind), remotes and their branches, tags, stashes, submodules, worktrees; filter box | P0 | US-GIT-1 |
| F-GIT-3 | Commit graph: lanes, branch/tag labels, HEAD marker, author/date/message columns, virtualized for 100k+ commits, search by message/author/hash/path, "jump to HEAD" | P0 | US-GIT-1 |
| F-GIT-4 | Commit details: metadata, parents, file tree of changes, per-file diff (uses the editor's diff component), open file at revision, copy hash | P0 | US-GIT-1 |
| F-GIT-5 | File history and folder history (log follow renames), from Solution Explorer and editor tab context menus | P1 | US-GIT-1 |
| F-GIT-6 | Blame view (full file, by line age shading) + inline blame in the editor (F-EDT-43) | P1 | US-GIT-5 |

## Working tree

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-GIT-10 | Local changes view: unstaged / staged lists with status icons, tree or flat, filter | P0 | US-GIT-2 |
| F-GIT-11 | Stage/unstage by file, hunk, or line selection (patch built server-side, applied with `git apply --cached`); discard with confirmation (to trash where possible) | P0 | US-GIT-2 |
| F-GIT-12 | Commit composer: summary + body, character guides, amend toggle, sign-off, commit templates, recent messages | P0 | US-GIT-2 |
| F-GIT-13 | AI commit message: drafts from the staged diff (role `titler` or `primary`), editable, never auto-commits | P1 | US-GIT-2 |
| F-GIT-14 | Conflicts list with 3-way merge editor (F-EDT-73): ours/theirs/base, accept per hunk, AI "propose resolution" producing an editable result | P1 | US-GIT-3 |

## Branching and sync

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-GIT-20 | Fetch (all/one remote, prune), pull (merge / rebase / ff-only per setting), push (set upstream, force-with-lease only, confirmation), with progress | P0 | US-GIT-2 |
| F-GIT-21 | Create/checkout/rename/delete branch; checkout with dirty tree -> stash/carry/cancel prompt | P0 | US-GIT-2 |
| F-GIT-22 | Merge (ff / no-ff / squash), cherry-pick, revert, reset (soft/mixed/hard with explicit confirmation), tags (create/push/delete) | P1 | US-GIT-3 |
| F-GIT-23 | Rebase onto; interactive rebase UI (Fork-style list: pick/reword/squash/fixup/drop/reorder by drag), abort/continue | P1 | US-GIT-3 |
| F-GIT-24 | Stash: create (incl. untracked, keep index), apply/pop/drop, show diff | P0 | US-GIT-1 |
| F-GIT-25 | Credential prompts surfaced in the UI when Git Credential Manager needs interaction (device flow URL / code displayed; never typed by IED) | P0 | US-GIT-2 |

## Scale and game-dev specifics

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-GIT-30 | Git LFS: detect, show LFS-tracked files and pointer-vs-content status, `lfs pull/fetch` actions, warnings for large untracked binaries | P1 | US-GIT-4 |
| F-GIT-31 | Submodules: status, update/init, open submodule as repo | P1 | US-GIT-4 |
| F-GIT-32 | Worktrees: list/create/remove; open a worktree as a workspace; "run this agent in a new worktree" (F-GIT-33) | P1 | US-GIT-4, US-GIT-6 |
| F-GIT-33 | Agent isolation: a chat can be bound to its own worktree + branch so parallel agents never edit the same checkout; merge-back flow with review | P1 | US-GIT-6 |
| F-GIT-34 | Performance: fsmonitor/untracked-cache hints for big repos, status debounced from file watching, graph paging | P1 | - |

## AI and integrations

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-GIT-40 | Git tools for the AI (T-GIT-*): status, diff, log, show, blame (read-only by default); stage/commit/branch gated by permission mode | P0 | US-TOOL-1 |
| F-GIT-41 | "Review changes with AI": the working tree diff or a commit range sent to a chat with a review prompt | P1 | US-INL-2 |
| F-GIT-42 | Hosting integration (GitHub PRs: list, checkout, create, review comments) as a separate optional plugin | P2 | - |
| F-GIT-43 | Source control provider interface for other VCS plugins (Perforce, Fossil) - the predecessor IDE had both | P2 | - |
