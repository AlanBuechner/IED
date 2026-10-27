# 31 - Nuke packages: versioned code from git, developed in place

## Purpose

Owner, 2026-10-27: "a generic package manager for the nuke build system that can be used to download packages from git
repos including private git repos with version control and sub packages", for C, C++ and C4 alike, so common code -
Claymore's windowing, renderer, ECS, asset manager, UI and DCC modules - lives in one private repository and every game
takes the parts it needs at the versions it wants. Developing a package together with the program that uses it must
feel like one codebase ("i dont want to spend my time going into a package making changes then pushing it updateing the
version on the software re pulling only to find out it does not work then doing it all over again"). It must be fast,
handle chains of dependencies, get along with the package managers languages already have (Go, Cargo), and IED must keep
two projects from making conflicting changes to a shared package.

The user reference is [`Nuke/README.md`](../Nuke/README.md) section 12; this doc is the design and its reasons.

## 1. The model

| Term | Is |
|---|---|
| **repository** | a git repository of packages. Any URL git can fetch: https, ssh, a local path |
| **package** | a folder of a repository whose `nuke.toml` has a `[package]` table with its `version`; named by its folder in the repository (`Windowing`, `Renderer/RHI/DX12`). Its projects are its own folder's `[project]` and every project folder below it that is not inside another package |
| **source** | what a dependency names: a package - the repository's URL followed by the package's folder (`https://github.com/AlanBuechner/Claymore.git/Renderer/RHI/DX12`) - or a local folder (a project of the same package) |
| **version** | a package's `[package] version`, and the git tag `<package folder>/v<semver>` (`Windowing/v1.2.0`), or `v<semver>` for the repository as a whole - Go's convention for modules in subfolders, so a Go module in a package folder has the same versions for `go get` |
| **versioned package** | a package that has tags of its own, or the repository root: the unit a version is chosen for. A package inside it without tags of its own is released with it, from the same commit |
| **lock** | `packages/nuke.lock`: the exact commit of every versioned package the workspace uses. Generated from the manifests, ignored with the packages folder |
| **locked** | a package installed read-only at the lock's commit: the default |
| **writable** (develop) | a repository a workspace edits: a git clone inside the workspace, built from its working tree - `writable = true` on a dependency, or `nuke package develop` on one machine |

A project says what it uses in its own `nuke.toml`, one form for everything, as Cargo's dependencies do - and nothing
else is written: any Nuke command pulls it.

```toml
[dependencies]   # name = { source, versions accepted } - private repositories: git's own credentials
RhiD3D12  = { source = "https://github.com/AlanBuechner/Claymore.git/Renderer/RHI/DX12", version = "~0.3.1" }
Windowing = { source = "https://github.com/AlanBuechner/Claymore.git/Windowing", version = "^0.2", writable = true }
Utils     = { source = "git@github.com:me/utils.git/Utils", branch = "main" }      # no release yet: a branch's tip
Strings   = { source = "git@github.com:me/utils.git/Strings", rev = "1a2b3c4d" }   # or one commit
Core      = { source = "../Core" }                                                # a project of the same package
```

The name is the entry's own (messages and `nuke package remove` use it). A dependency's projects are linked to the
project by themselves; `depends` stays only for workspaces that name their own projects that way. `writable = true`
makes the build clone the package's repository into the workspace (one clone per repository: all its packages build from
it); without it every package is installed locked. Taking `writable` away returns the repository to locked installs at
the next build, which refuses while the clone has uncommitted changes or unpushed commits.

A project is compiled into a program only when something depends on it, so **which packages a game uses is what its
projects name**: a game that names `Windowing/Win32` and not `Windowing/Linux` gets the Win32 backend and never
resolves what the Linux one needs.

A package says what it needs the same way - every package, its own repository's too, by URL - and only a project of
the same package by its folder:

```toml
# Renderer/RHI/DX12/nuke.toml in the claymore repository
[package]
version = "0.2.1"
description = "The Direct3D 12 backend of the RHI"

[project]
name = "RhiD3D12"
kind = "module"

[dependencies]
Rhi     = { source = "https://github.com/AlanBuechner/Claymore.git/Renderer/RHI", version = "^0.2.1" }
Agility = { source = "https://github.com/me/D3D12Agility.git/SDK", version = "^1.614" }
```

A local dependency that leaves its package is refused: whoever uses a package must get everything it uses, so a package
reaches another package only by its URL. The repository's own workspace takes the packages its dependencies name on
its own repository (one of the checkout's remotes) from its folders, so it builds and tests all of its packages together
before it releases them.

## 2. Resolution

1. **Requirements.** The workspace's projects' `[dependencies]`, and every used package's. A version
   requirement is Cargo's: `1.2` and `^1.2` take any later version with the same major (or the same minor below 1.0),
   `~1.2.3` the same minor, `=1.2.3` one version, `>=1.2, <1.5` a range, `*` any. Prerelease versions only when a
   requirement names one.
2. **Nothing is inferred.** A package's `[dependencies]` at the chosen version are the requirements it adds; a project
   of it that depends on another package's project without naming that package is an error that says what to write.
3. **Choice.** The highest version that satisfies every requirement on a versioned package. A workspace that has a lock
   keeps each locked version that still satisfies them (adding a package never moves another); `nuke package update`
   chooses the highest again. An update never moves a package back to an older commit than its lock's (a lock on an
   unreleased commit after `1.2.0` is not "updated" to `1.2.0`).
4. **Conflicts.** When the requirements on a package exclude each other, Nuke tries the next lower versions of the
   packages that made them, then stops with every requirement and who made it:
   `Windowing: the workspace wants ^0.3, Renderer 0.4.1 wants ^0.2 (from its commit 1a2b3c4d)`.
5. **Names.** A package's projects join the workspace; a project name that two of them, or the workspace and one of
   them, both have is an error naming both, as two projects of one workspace with one name are.

## 3. Speed

- **A build never touches the network.** When `nuke.lock` matches the manifests (it keeps a digest of the requirements
  it was made from) and every locked package is in place, Nuke only reads files: no git process starts.
- **One query per source.** Choosing versions needs only the tags: `git ls-remote --tags` once per source (in parallel
  for several), not a clone.
- **One copy of each repository per machine.** A bare mirror in Nuke's cache (`<nuke home>/.cache/packages/git/`,
  `NUKE_PACKAGE_CACHE`) fetched only when a needed commit is missing; manifests are read from it with
  `git cat-file --batch` without checking anything out; a locked package is exported from it with `git archive` of its
  folder at its commit.
- **Only what is used.** A package's dependencies are resolved only when one of its projects is used.

## 4. Where packages live

```text
MyGame/
  nuke.toml            [workspace]; each project's nuke.toml has its [dependencies]: the files you write
  packages/            Nuke's, all generated (in .git/info/exclude; [workspace] packages-dir changes the name)
    nuke.lock          the commit of every package: written from nuke.toml
    claymore/
      Windowing@0.2.0/ a locked package: its folder (Windowing/) at its commit, files read-only
      Renderer@0.3.1/
    utils/             a source being developed: a git clone of the whole repository
    nuke-packages.json what is installed and what is developed (machine-local)
```

Packages are inside the workspace because IED edits only files of the workspace (files outside it open read-only,
`docs/10`), and so they show in the Solution Explorer, the file tree, search and the language server like the
workspace's own projects. The folder is kept out of git by the checkout's own `.git/info/exclude` rather than
`.gitignore`, because search in IED and the AI's tools skip what `.gitignore` files ignore. The cache shares the git
objects among every workspace of the machine.

## 5. Developing a package with the program

`nuke package develop claymore` makes `packages/claymore/` a clone of the repository at the tip of its default branch,
and from then on every claymore package of the workspace builds from that working tree: edit the game and the engine
modules side by side, build, run, debug, commit each repository when it works. There is no publish step in the loop:

- **The lock follows the checkout.** Every Nuke command records the checkout's HEAD commit in the lock, with the
  version it is (`0.2.0`) or a pseudo-version after the last release (`0.2.0+3.1a2b3c4d`: three commits later).
- **Releasing is optional and one command.** `nuke package release Windowing --minor` (in the clone or in the
  repository's own checkout) tags `Windowing/v0.3.0` at HEAD and pushes the branch and the tag; it refuses while the
  checkout has uncommitted changes or is behind its remote, and when a package of the same repository that Windowing
  uses has unreleased changes (it names them: `release Rhi too`). A package of the repository whose newest release would
  not accept the new version (Input 0.1.0 needs Windowing ^0.1.0, and 0.2.0 is outside it) is released again at the same
  commit (Input 0.1.1), so a release of it accepts the new one; the workspace's own requirement moves when the new
  version is outside it. Other games move to the new version with `nuke package update`.
- **The lock moves only for what changed.** While a source is developed, a versioned package whose folder is the same at
  HEAD as at its last release stays locked at that release; a commit that changes Windowing moves only Windowing's entry.
- **Back to locked.** `nuke package lock claymore` exports the lock's commits and removes the clone - only when every
  change in it is committed and pushed (it names what is not).
- **A checkout elsewhere.** `nuke package develop claymore --at C:/GameDev/Claymore` builds from an existing clone
  outside the workspace instead (for Nuke alone; IED opens files outside a workspace read-only).

## 6. One writable copy: no conflicting changes

The owner asked that IED "lock out files from being written to if they are not the latest version of the package" so
that two projects never change a common module in conflicting ways. The rule Nuke and IED keep:

1. **A locked package is never edited.** Its files are read-only on disk (any editor, script or tool is refused), and
   IED opens them read-only with the reason and the way out: `Windowing 0.2.0 is a locked package of claymore - Develop
   the package to change it`. Editing an old version in place would make a fork no release can carry.
2. **A develop checkout is writable only while it is the latest.** When its branch is behind its remote - another
   workspace or machine pushed changes to the package since - IED opens its files read-only (`claymore is 3 commits
   behind origin/main - Pull to edit it`) until it is brought up to date; its own unpushed commits do not lock it. So
   every change starts from the newest code, and two workspaces that develop the same source stay one line of history:
   the second one to push has to take the first one's work before editing again, exactly where git would make it merge.
3. **Nuke knows every checkout of a source on the machine.** `nuke package status` names the other workspaces developing
   the same source with uncommitted changes, which no remote can show yet.

Nuke writes the state IED needs on every command: `.ied/local/packages.json` (each package's folder, version, mode,
projects and whether it is writable; each develop clone's branch and how far it is ahead of and behind its remote) and
`.ied/local/readonly/nuke.json` (the folders no one may write, each with the reason and the way out). "Behind" is what
the clone last fetched: `nuke package status --fetch` fetches first, and IED's `nuke` plugin runs it by itself
(2026-10-27, `plugins/nuke/code/`): in each trusted workspace that uses packages (its `nuke.toml` has `[dependencies]`,
or it has a lock), when the workspace opens or is trusted and then every
`nuke.packages.fetchMinutes` (default 5; 0 turns it off). IED's server reads the file Nuke rewrites through its watcher
(`docs/10` "Read-only folders"), so a clone that falls behind its remote turns read-only in the editor with no Nuke
command run by hand; the plugin parses and enforces nothing itself. A fetch that fails (no network, a sign-in missing)
is told once in the workspace's IED output, and again only after one has worked. The fetch fetches every clone even
when one of them cannot be fetched, so the others are still marked, and git never asks for a sign-in under Nuke - not
on the terminal and not in the credential manager's window (`GCM_INTERACTIVE=never`) - so the timer never opens one;
`git ls-remote <url>` in a terminal signs in once. Checked by `tools/server-checks/tests/test_nuke_plugin.py` (a fake
`nuke.command`) and Nuke's `test_packages.py`.

## 7. Other package managers

Native first (`docs/23` N-15): a Rust or Go project keeps its `Cargo.toml` / `go.mod`, lockfile and cache, and
fetches its crates and modules itself; Nuke never resolves them and never writes those files. Nuke's tags are Go's
module tags, so a Go module in a package folder is the same version to `go get` and to Nuke, and Cargo's git
dependencies take the same tag (`tag = "Windowing/v0.2.0"`).

Developing a source reaches those tools too, through their own ways of building local code in place of a release,
never through their manifests (P-10). The language's driver does it at each build or test, from the workspace's develop
checkouts (`ctx.checkouts`), in a folder of the project's under `build/develop/`:

- **Go.** A project whose `go.mod` requires a module that lives in the clone (the module paths of the clone's `go.mod`
  files; a required clone module's own requirements are followed) builds with `GOWORK` naming
  `build/develop/<project>/go.work`: `use` lines for the project, those module folders, and whatever the project's own
  `go.work` uses (its `replace` lines too), with absolute paths. Workspace mode builds the clone's folders in place of
  the required versions; `go.mod` and `go.sum` stay as they are. A project that requires none gets no `go.work`.
- **Rust.** A project that gets crates from the source's repository - a git dependency in its manifests
  (`cargo metadata --no-deps`) or a git package in its own `Cargo.lock`, the URL in any spelling of the repository
  (`https://`, `ssh://`, `git@host:`, a path or `file://`) - builds with `cargo --config build/develop/<project>/cargo.toml`:
  `[patch."<url>"]` points each of those crates at its folder in the clone, and `resolver.lockfile-path` keeps the
  patched resolution in a `Cargo.lock` beside it, copied from the project's whenever the project's is newer. The
  project's `Cargo.lock` stays as it is (a cargo without that setting writes the patched resolution into it while the
  source is developed; cargo 1.98 has it).
- **The editor.** IED's language servers follow the clone too, so go-to-definition, hover and diagnostics in a Go or
  Rust project land in `packages/<source>/` (editable) rather than in the module cache or Cargo's git checkouts. While
  a source is developed, every build, test or package command - `nuke package develop` itself, IED's periodic `nuke
  package status --fetch` - has each driver's `editor(folder, contexts)` write files of the whole workspace into
  `build/develop/` (`Nuke/nuke/develop_files.py`): for Go a `go.work` using every Go project of the workspace (gopls
  serves them all with it) and the clone modules they require, and `gopls.json`, `{"build.env": {"GOWORK": ...}}`; for
  Rust a `cargo.toml` with the `[patch]` tables of every Rust project, and `rust-analyzer.json`, `cargo.configPath`
  naming it (rust-analyzer passes it to every cargo command, `cargo metadata` included) with
  `cargo.extraEnv.CARGO_RESOLVER_LOCKFILE_PATH = "target/nuke-develop/Cargo.lock"` - relative, so each Cargo workspace
  resolves in a lockfile of its own in its `target/`, copied from its `Cargo.lock`, and rust-analyzer's `cargo check`
  never writes the project's. The go and rust plugins' profiles read them (`initializationOptionsFile`), and IED starts a
  server again when its file appears, changes or goes (`docs/24` "One options file"); each file holds a digest of the
  `go.work` or `cargo.toml` (`NUKE_DEVELOP`), so a changed one starts the server again too.
- **Back to locked.** `nuke package lock` removes `build/develop/` first - IED then starts gopls and rust-analyzer
  again without the clone, and rust-analyzer lets go of the clone's files - and waits a while for any program still
  holding a file of the clone before it moves it aside; when one does not let go, it changes nothing and says what to
  close. The next command points the tools again at whatever is still developed.

`go` or `cargo` run by hand in a terminal still see the releases: the files are handed to the tools by Nuke and IED,
never put where the tools look by themselves (a `go.work` or `.cargo/config.toml` in the project would be the
project's own, and committed).

## 8. Packages in IED

Every project of a repository of packages is a package (its `nuke.toml` has a `[package]` table), so IED shows the
packages where it shows projects, and manages them in a window of their own.

**Solution Explorer.** `nuke init --ied` (and every package command) writes a package manifest that holds no project -
a versioned package's folder above its projects, such as `Renderer/nuke.toml` - as an item of the solution folder it
belongs to, and the projects a workspace gets from packages under `Packages/<source>/<folder>`. Each project has a
**Dependencies** node: **Packages**, the packages its `depends` reach (name, version, source, locked or developed,
a lock marker when read-only; opening one opens its page at that version on the source's host), and **Projects**.

**The Package Manager** (Tools > Manage Packages..., or a solution, project or package's context menu; the `nuke`
plugin's `ui/` and `code/PackageManager.cf`) is Visual Studio's NuGet window for Nuke packages:

| Tab | Shows | Does |
|---|---|---|
| Installed | every versioned package of the lock: version, locked or developed, a newer release | the selected one's details: description, source and links (its repository, its files at this version, the commit), the requirement that brings it or the packages that do, its folder, sub-packages and projects, **Depends on** (each requirement and the version it reached) and **Required by** (the workspace and the packages that need it, with what they accept) |
| Updates | the packages with a newer release: one `update` reaches (within the locked version's major), or a new major only a changed requirement takes | Update all (`nuke package update`), or one |
| Browse | a source's packages at its default branch (`nuke package browse`): every released version, whether the workspace has it | Install a chosen version |
| Repositories | the repositories the workspace's projects name, and those only packages' dependencies name | browse |

Choosing a version writes a requirement: `^version` for the newest release of its line (later fixes still come with
updates), `=version` for an older one (`^` would keep the newer release locked); the line under the picker says which.
The details also Develop a source, Pull a clone that is behind, Release a developed package (patch, minor or major,
after a confirmation: it pushes) and Lock it again, and Remove a requirement - each a `nuke package` command, shown with
its output below the list.

The window holds no state of its own (`docs/01`): the plugin's code keeps each workspace's last status, what each source
offered, the command running and the last one's output on the topic `packages:<wsId>`, and runs the commands one at a
time in that workspace (trusted) after what is queued there, through three bindings that answer at once:
`nuke.packages.refresh {wsId, fetch?}` (`status --json`), `nuke.packages.browse {wsId, source, offline?}` and
`nuke.packages.run {wsId, args}` (only the actions above). So a closed browser loses nothing: a release goes on, and its
output is there when the window opens again on any device. The status is read again after each action, and when the
manifest, the lock or `.ied/local/packages.json` changes by other hands (a terminal, the AI's `nuke_package` tool,
the timed fetch) in a workspace whose window was opened.

## 9. Decisions

| # | Decision | Why |
|---|---|---|
| P-1 | A package is a folder with a `[package]` table in its `nuke.toml`, named by its path in the repository; no registry and no index file | The repository is the registry: nothing to publish to, nothing to keep in step; a private repository needs nothing but git access |
| P-2 | Versions are git tags `<path>/v<semver>` (or `v<semver>` for the whole repository); a package without tags of its own is released with the nearest package above it that has some | One repository holds many independently versioned packages, or one version for all; Go uses the same tags, so Go modules and Nuke packages share versions; `ls-remote` alone lists every version |
| P-3 | Every dependency is written in `[dependencies]` - a package by its repository's URL and folder, its own repository's too; a local folder only within the same package (refused outside it) - and nothing is inferred (owner, 2026-10-27: "a package could depend on another package in another repo"; "one system that will handle both local packages and remote packages"). Earlier: same-repository dependencies were inferred from `depends` and tags | A package manager can follow links it reads, across repositories; a package carries what it uses |
| P-4 | The workspace lists versioned packages; which of their projects and sub-packages are used follows the projects' `depends` | "Optional submodules for what platforms you want to support" without a feature system: a backend no project depends on costs nothing, and its own dependencies are never resolved |
| P-5 | The lock (`packages/nuke.lock`) holds commits, and a develop checkout's HEAD goes into it on every command; it is generated from `nuke.toml` and ignored with the packages (owner, 2026-10-27: "the lock file is a generated file so it should be ignored") | A checkout keeps the versions it chose until `update` or a changed requirement, and builds offline; `nuke.toml` is what the program commits - a version it must build with exactly is `=x.y.z` there. Earlier it was committed beside `nuke.toml`, so a game's commit pinned every package's commit; the owner made `nuke.toml` the only file a program keeps |
| P-6 | Packages are installed inside the workspace (`packages/`), locked ones read-only on disk; mirrors are shared per machine | IED edits only workspace files; read-only on disk stops every writer, not only IED; mirrors make a second workspace's install a local copy |
| P-7 | Develop is per source and per workspace: the whole repository becomes a clone in the workspace | A repository's packages are developed and tested together (its own workspace builds them all); one checkout per source keeps them on one commit |
| P-8 | Files of a package are writable only in a develop checkout that is not behind its remote | The owner's lock-out, sharpened: there is exactly one writable copy of a package line, and it is the newest |
| P-9 | Nuke resolves only Nuke packages; language package managers keep their own dependencies | Native first (N-15); Nuke adds the cross-language graph, not a second resolver for crates and modules |
| P-10 | A develop checkout reaches Go and Rust through the tools' own overrides - `GOWORK`, and cargo's `--config` with `[patch]` and `resolver.lockfile-path` - written per project in `build/develop/`, never through `go.mod`, `Cargo.toml` or their lockfiles; IED's gopls and rust-analyzer get workspace-wide ones through their profiles' `initializationOptionsFile` | Developing a package must not change what the program commits, and one codebase must hold for every language and in the editor: the tools build the clone the way their own users would, one language server serves the workspace as before, and removing one folder brings back the releases |
| P-11 | The manifests alone say what a workspace uses - each project's `[dependencies]` names a source (`"<repository URL>/<folder>"`) and whether it is `writable` - and any Nuke command, a build first of all, pulls and clones to match; the `nuke package` commands are conveniences over the same files | The owner: "give it a package at a git url and list a version, then when I build it pulls that package down, write-locked or writable if it says so" - Cargo's model. What a checkout of the workspace builds is what its committed manifest says, on every machine |
