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
| **source** | a git repository of packages, named in a workspace's `[sources]` (`claymore = "https://github.com/AlanBuechner/Claymore.git"`). Any URL git can fetch: https, ssh, a local path |
| **package** | a folder of a source whose `nuke.toml` has a `[package]` table; named by its path in the repository (`Windowing`, `Renderer/RHI/DX12`). Its projects are its own folder's `[project]` and every project folder below it that is not inside another package |
| **version** | a git tag: `<package path>/v<semver>` (`Windowing/v1.2.0`), or `v<semver>` for the repository as a whole - Go's convention for modules in subfolders, so a Go module in a package folder has the same versions for `go get` |
| **sub-package** | a package inside another one. Without tags of its own it is released with the nearest package above it that has some (the repository's `v` tags at the top); with its own tags it is versioned on its own |
| **versioned package** | a package that has tags of its own, or the repository root: the unit a version is chosen for. Every package released with it comes from the same commit |
| **lock** | `nuke.lock` beside the workspace's `nuke.toml`: the exact commit of every versioned package the workspace uses. Committed |
| **develop** | a source a workspace edits: a git checkout of the repository inside the workspace, built from its working tree |

A workspace says what it uses:

```toml
[sources]
claymore = "https://github.com/AlanBuechner/Claymore.git"   # private: git's own credentials (credential manager, SSH keys)

[packages]
Windowing = "claymore@^0.2"                                # short form: <source>@<version requirement>
Renderer  = { source = "claymore", version = "~0.3.1" }
Utils     = { source = "utils", branch = "main" }          # no release yet: a branch's tip (the lock pins its commit)
Strings   = { source = "utils", rev = "1a2b3c4d" }         # or one commit
```

and its projects use the packages' projects by name, as they use their own: `depends = ["Windowing", "Win32", "RhiD3D12"]`.
A project is compiled into a program only when something depends on it, so **which sub-packages a game uses is what its
projects depend on**: a game that depends on `Win32` and not `Linux` gets Windowing's Win32 backend and never resolves
what the Linux one needs.

A package says what it needs from other sources; what it needs from its own repository Nuke reads from its projects'
`depends`:

```toml
# Renderer/RHI/DX12/nuke.toml in the claymore repository
[package]
description = "The Direct3D 12 backend of the RHI"

[package.dependencies]
Agility = { source = "https://github.com/me/D3D12Agility.git", version = "^1.614" }

[project]
name = "RhiD3D12"
kind = "module"
depends = ["Rhi", "Agility"]
```

The repository's own workspace uses every package's dependencies, so the repository builds and tests all of its packages
together before it releases them.

## 2. Resolution

1. **Requirements.** The workspace's `[packages]`, and every used package's `[package.dependencies]`. A version
   requirement is Cargo's: `1.2` and `^1.2` take any later version with the same major (or the same minor below 1.0),
   `~1.2.3` the same minor, `=1.2.3` one version, `>=1.2, <1.5` a range, `*` any. Prerelease versions only when a
   requirement names one.
2. **Inferred requirements inside a repository.** A package whose project depends on a project of another package of
   the same repository needs that package. Released with the same versioned package, it comes from the same commit;
   versioned on its own, it needs at least the version that package had at the dependent's commit (the latest of its
   tags in that commit's history), with the same major. Nothing to write: the repository's projects already say it, and
   it holds at every commit.
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
  nuke.toml            [workspace], [sources], [packages]
  nuke.lock            committed
  packages/            Nuke's (in .git/info/exclude; [workspace] packages-dir changes the name)
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

- **The lock follows the checkout.** Every Nuke command records the checkout's HEAD commit in `nuke.lock`, with the
  version it is (`0.2.0`) or a pseudo-version after the last release (`0.2.0+3.1a2b3c4d`: three commits later). A commit
  of the game therefore pins exactly the package code it was built with, released or not; another machine builds the
  same commit once it is pushed (Nuke says so when it is not).
- **Releasing is optional and one command.** `nuke package release Windowing --minor` (in the clone or in the
  repository's own checkout) tags `Windowing/v0.3.0` at HEAD and pushes the tag; it refuses while the checkout has
  uncommitted changes or is behind its remote, and when a package of the same repository that Windowing uses has
  unreleased changes (it names them: `release Rhi too`). Other games move to the new version with `nuke package update`.
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
the clone last fetched: `nuke package status --fetch` fetches first (a periodic one from IED's `nuke` plugin is a
follow-up: the plugin has no code yet).

## 7. Other package managers

Native first (`docs/23` N-15): a Rust or Go project inside a package keeps its `Cargo.toml` / `go.mod`, lockfile and
cache, and fetches its crates and modules itself; Nuke never resolves them. Nuke's tags are Go's module tags, so a Go
module in a package folder is the same version to `go get` and to Nuke, and Cargo's git dependencies take the same tag
(`tag = "Windowing/v0.2.0"`).

## 8. Decisions

| # | Decision | Why |
|---|---|---|
| P-1 | A package is a folder with a `[package]` table in its `nuke.toml`, named by its path in the repository; no registry and no index file | The repository is the registry: nothing to publish to, nothing to keep in step; a private repository needs nothing but git access |
| P-2 | Versions are git tags `<path>/v<semver>` (or `v<semver>` for the whole repository); a package without tags of its own is released with the nearest package above it that has some | One repository holds many independently versioned packages, or one version for all; Go uses the same tags, so Go modules and Nuke packages share versions; `ls-remote` alone lists every version |
| P-3 | Same-repository dependencies are inferred from projects' `depends` and the tag history; other sources' are declared in `[package.dependencies]` | The repository already says which of its projects use which; writing it twice would drift. Another source needs a URL and a requirement, which nothing else gives |
| P-4 | The workspace lists versioned packages; which of their projects and sub-packages are used follows the projects' `depends` | "Optional submodules for what platforms you want to support" without a feature system: a backend no project depends on costs nothing, and its own dependencies are never resolved |
| P-5 | `nuke.lock` holds commits, and a develop checkout's HEAD goes into it on every command | Builds are reproducible from the game's commit alone, released or not, and offline; developing a package never needs a release to keep the game's history exact |
| P-6 | Packages are installed inside the workspace (`packages/`), locked ones read-only on disk; mirrors are shared per machine | IED edits only workspace files; read-only on disk stops every writer, not only IED; mirrors make a second workspace's install a local copy |
| P-7 | Develop is per source and per workspace: the whole repository becomes a clone in the workspace | A repository's packages are developed and tested together (its own workspace builds them all); one checkout per source keeps them on one commit |
| P-8 | Files of a package are writable only in a develop checkout that is not behind its remote | The owner's lock-out, sharpened: there is exactly one writable copy of a package line, and it is the newest |
| P-9 | Nuke resolves only Nuke packages; language package managers keep their own dependencies | Native first (N-15); Nuke adds the cross-language graph, not a second resolver for crates and modules |
