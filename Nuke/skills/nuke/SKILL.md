---
name: nuke
description: Building, running and testing C4 workspaces with Nuke - the nuke command line (chained commands, configurations, projects), nuke.toml workspace and project manifests, custom build steps and commands, versioned C4 toolchains, and what to do when the compiler itself is wrong.
triggers:
  - "nuke build run test"
  - "nuke.toml"
  - "add a build step"
  - "custom nuke command"
  - "which C4 toolchain"
  - "compiler bug"
---

# Nuke

Nuke builds C4 workspaces. It lives in the IED checkout (`<IED>/Nuke`); `<IED>/bin` on PATH gives the `nuke`
command in any folder. Full reference: `<IED>/Nuke/README.md`. Inspect anything with `nuke info`,
`nuke list`, `nuke help <command>` (add `--json` for machine-readable output).

## Commands

    nuke build [CONFIG] [PROJECT...]   build (default: the workspace's default project) and its dependencies
    nuke run [CONFIG] [-- ARGS]         build, then run; exits with the program's exit code
    nuke test [--filter "math.*"]       build and run every [test] harness (or the named projects')
    nuke clean | rebuild                clean this configuration's outputs / clean then build
    nuke build run dev                  commands chain left to right; each project builds once
    nuke init [NAME] [--kind ...]       new workspace here, or a new project inside one
    nuke toolchain list|which|install|use|remove

Configurations: Debug (`debug`, -O0 -g), Development (`dev`, -O3 -g, the default), Dist (`dist`/`release`, -O3).
Outputs: `build/bin/<Config>-windows-x64/<Project>/`, intermediates in `build/int/...`.

## Manifests

The workspace root has `nuke.toml` with `[workspace]` (name, `toolchain`, `default-project`, optional
`projects` list, `[commands.<name>]`); each project folder has `nuke.toml` with `[project]`:
`kind` (`executable` | `module` | `custom`), `depends`, `sources` (default `["src"]`), `includes`,
`defines`, `links`, `compiler-args`, `[[project.copy]]`, `[project.config.<Config>]` overrides and
`[[project.steps]]`. Unknown keys are errors - read the message, it names the file and key.

A module's sources compile into every program that depends on it; each project keeps its own defines.
Tests are `[test]` functions under `src/tests/`; `nuke test` compiles them into `<Project>-tests.exe`.

## Steps and commands

    [[project.steps]]
    phase = "pre-build"                 # pre-/post- any command, or the command itself
    run = ["${python}", "tools/cook.py", "${project.dir}/assets", "${bin}/assets"]
    inputs = ["assets/**"]              # with outputs: skipped while outputs are newer
    outputs = ["${bin}/assets/**"]
    configs = ["Dist"]                  # optional

A workspace adds commands with `[commands.kit]` (description, `requires = ["build"]`, `steps`); projects
attach steps to them with `phase = "kit"`. Python steps: `python = "file.py:function"`; extension scripts
listed in `scripts = [...]` define `register(nuke)` and call `nuke.command(...)` / `nuke.step(...)`.
Variables: `${workspace.dir} ${project.dir} ${project.name} ${config} ${arch} ${bin} ${int} ${output}
${python} ${nuke} ${toolchain.dir} ${c4} ${env:NAME}`, and `${args}` (the words after `--`).

## Toolchains and compiler bugs

`toolchain = "0.1.0"` pins an immutable install from `<IED>/toolchains/c4/`; `"dev"` is the compiler built
from `<IED>/C4` by its latest Development build; `"latest"` the newest numbered one. If C4 itself is wrong
(a miscompile, a crash, a bad diagnostic, a Bedrock bug), fix it at the source instead of working around it:

1. Reproduce it in a small program and add an entry to `<IED>/C4/docs/status/known-bugs.md`.
2. Fix it in `<IED>/C4`, add a test that would have caught it, run `nuke build` and `nuke gates` in `<IED>`
   (the build refreshes `dev`), then remove the known-bugs entry.
3. Continue in your workspace: with `toolchain = "dev"` the fix is live now; for a pinned version bump
   `C4/C4Lib/src/Compiler/Version.h`, run `nuke install-c4` in `<IED>`, then `nuke toolchain use <version>`.
