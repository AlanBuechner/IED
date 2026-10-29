# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-434**

## Rules for this file

- **One entry per bug.** If one root cause has several symptoms, list them in one entry.
- **IDs are permanent.** Take the "Next ID" above and bump it. Never renumber or reuse an ID, so
  commits, tests and code comments can refer to `BUG-012` safely.
- **Remove the entry when it is fixed**, in the same change as the fix. The fix must come with a test
  that would have caught it (see [../tools/05-test-suites.md](../tools/05-test-suites.md)). Also remove
  the matching "Current implementation" note on the feature's page. The commit message keeps the history.
- **Pre-existing is not a reason to keep a bug.** Per `AGENTS.md`, every known bug gets fixed.
- **Status** is `Open` (confirmed by a repro or by reading the code) or `Needs re-test` (reported
  earlier, may already be fixed; re-test, then fix or delete).
- **Severity:**
  - **High** - miscompiles, memory unsafety, compiler crash, or valid programs that cannot be written.
  - **Medium** - wrong behaviour with a workaround, valid code rejected, a language rule not enforced.
  - **Low** - diagnostics quality, performance, tooling polish.
- Design questions that are not bugs go to [roadmap.md](roadmap.md). Unbuilt features go to the
  roadmap backlog, not here.

### Entry template

```text
### BUG-NNN Short title
- **Area:** Parser | Model | HIR | Codegen | Preprocessor | Runtime | Bedrock | Tools | Diagnostics | Tests
- **Severity:** High | Medium | Low
- **Status:** Open | Needs re-test
- **Found:** YYYY-MM-DD
- **Symptom:** what goes wrong, from the user's point of view
- **Repro:** smallest program or command (optional)
- **Where:** files / functions involved
- **Notes:** fix direction, related bugs, constraints (optional)
```

---

## Index

| ID | Title | Area | Severity |
|---|---|---|---|
| BUG-421 | C4LSP puts a dependency's declarations in the root project of a multi-project program | Tools | Low |
| BUG-433 | C4LSP does not report an unknown name in a file-scope variable's initializer | Tools | Low |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

## Interop, runtime and Bedrock

## Tools

### BUG-421 C4LSP puts a dependency's declarations in the root project of a multi-project program
- **Area:** Tools
- **Severity:** Low
- **Status:** Open
- **Found:** 2026-10-28
- **Symptom:** In the Claymore workspace the editor shows, on `Packages/Renderer/RHI/src/Rhi/Frame/Renderer.ch` and
  `Commands/CommandList.ch`, `unresolved type for field 'm_Log' in 'RendererDesc': 'Log' is declared in project
  'ClaymoreClient', and project 'Rhi' does not depend on 'ClaymoreClient'` (and `type not found: Logger` in
  `Frame/ShaderWatcher.ch`), while C4.exe compiles the same files with the same build description
  (`build/int/Development-windows-x64/ClaymoreClient/projects.toml`, where `Log` is a project of its own that `Rhi`
  depends on) without an error. `Windowing::` names in the same files resolve. Another symptom, seen 2026-10-28 in the
  same workspace: every file of `Packages/Renderer/Render` and the files using `Render::` in `ClaymoreEngine` show
  `redefinition of namespace 'Render': it is already a function (RhiD3D12/Native.ch:16133)` (a function of the SDK
  headers RhiD3D12's `Native.ch` includes `extern "c++"`; Render does not depend on RhiD3D12) and then `not a
  namespace path: Render`, while `nuke test` builds the same programs clean.
- **Repro:** open Claymore (the package `Packages/Log` was added after the session's language server started) and
  read the problems of `Packages/Renderer/RHI/src/Rhi/Frame/Renderer.ch`; not yet reduced to a small workspace.
- **Where:** C4LSPLib `CompilationSession` (which program serves a file shared by several, `RegisterProjects`),
  `ProgramModel::OutOfProjectReach`.
- **Notes:** reduce it first: a two-program workspace whose shared module depends on a module added later. A third
  symptom (2026-10-29, same workspace): after `Procedural = { source = ... }` was added to `ClaymoreEngine`'s
  `[dependencies]`, `Scene/Pipeline/TerrainRenderer.ch` showed `include not found: 'Procedural/Terrain/Heightfield.ch'`
  for hours of builds that compiled it clean, though `.ied/local/lsp-options/ClaymoreEngine.json` (the programs the
  server is told through `initializationOptionsFiles`) was rewritten during them; it cleared when IED restarted. So
  either the pool's `SendOptionsFilesIfChanged` did not fire for that rewrite, or C4LSP took the new programs without
  re-resolving the includes of files it had analyzed: check `LspProgramsTest`'s configuration-change case with a
  dependency added to a program that already holds the file.

### BUG-433 C4LSP does not report an unknown name in a file-scope variable's initializer
- **Area:** Tools
- **Severity:** Low
- **Status:** Open
- **Found:** 2026-10-28
- **Symptom:** `static int32 s_ProbeBroken = UndefinedProbeName;` appended to a source of a library project
  (Claymore's `Packages/DCC/Geometry/src/Geometry/Primitives.cf`) gets no diagnostic from C4LSP - a hover on it answers
  from the new text, and nothing is published within 6 s - while C4.exe rejects it (`no variable named
  UndefinedProbeName`). The same line as `static UndefinedProbeType s_ProbeBroken;` is reported within 4 s, and errors
  inside function bodies of the same file are reported.
- **Repro:** append the first line above to any `.cf` of a project, open it in the editor: no squiggle; `nuke build`
  of a program using it fails.
- **Where:** C4LSPLib's analysis of a program (`Program::Analyze`, the session's semantic pass): file-scope variable
  initializers seem to be left unchecked where function bodies are checked.
- **Notes:** check whether namespace-scope `const` initializers are skipped too.
