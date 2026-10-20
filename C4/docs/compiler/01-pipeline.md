# 01 - The compilation pipeline

`C4.exe` compiles a whole program in one invocation: every input source plus the default library is
parsed once, analyzed into one program model, lowered, emitted as one LLVM module, compiled to one object
and linked with the runtime into an executable. There is no per-file object step and no separate link step
for C4 code. The orchestration lives in `src/Source.cpp` (`CompilerDriver::Run`); everything it calls lives
in `C4Lib`.

## Overview

```
 CLI args (BuildUtils::ParseArgs)
        |
        v
 [1] RegisterSources ---- user inputs (folders, recursive) + Bedrock umbrella next to C4.exe
        |
        v
 [2] RunPreprocessPhase
        |   MacroPass    : #define/#if/... text expansion, macro tables inherited down includes
        |   InteropPass  : extern "c"/"c++" include|source -> CBind bindings, foreign objects, libs
        |   effective text replaces each Source's text (matched by absolute path)
        v
 [3] CompileAll (CompilerInstance) -- parallel parse, each file exactly once
        |   include "x.ch" -> RequestFileInclude message, requester parks until x is parsed,
        |   x's include closure is folded into the requester's closure
        v
 [4] AnalyzeProgram
        |   MergeIncludes (LSP navigation only), DetectUserMain
        |   ModelBuilder: pass 1 REGISTER shells -> pass 2 RESOLVE fixed point -> FinalizeLayouts
        |   -> one ProgramModel; diagnostics = parse errors + model errors
        |   any front-end error stops here
        v
 [5] RunBackend: CodeGenDriver::Lower
        |   types -> function decls -> HIR LowerFunction + emit, to a fixpoint -> test harness (-test)
        |   -> @main wrapper -> __c4_global_init rounds -> globals -> reflection image
        |   -> __c4_gc_roots -> verify
        v
     <out>.ll  ----------------------------------- (-S / --emit-ir stops here)
        |
        v
 [6] clang -c -target x86_64-pc-windows-msvc -mstackrealign -fexceptions -O<n>  -> <out>.obj
 [7] runtime objects (GC, reflect, test, IO as C; EH as C++), cached in %TEMP%/c4_runtime
 [8] clang driver -> lld-link: main obj + runtimes + foreign objs + libs -> <out>.exe
```

`-ftime` prints the wall-clock time of the preprocess / parse / model / codegen phases.

## 1. Source registration

- Every CLI argument not starting with `-` is an input. A folder is scanned recursively for `.cf`/`.ch`; a
  single file registers its whole parent folder (so its includes resolve). Every registered folder is an
  include root. Zero sources is an error that explains the expected extensions.
- The default library is registered after user inputs from `<dir of C4.exe>/Bedrock/bedrock.ch` via
  `RegisterUmbrella`, which follows only the `include` graph reachable from the umbrella. Bedrock files that
  the umbrella does not reach never enter the build. Bedrock is always compiled, but its names are visible
  only to files that `include "bedrock.ch"` (see [Include visibility](#include-visibility)).
- Only `.cf` files are entry files (`CompilerInstance::IsEntryFile`): the model is built from entry files and
  their transitive include closures, and `main` is searched for in entry files.
- **Partial:** a run with no inputs at all compiles `assets/TestCode` with `--pretty-print` (a development
  convenience left in `main`). The Bedrock lookup is Windows-only.

## 2. Preprocess phase

### MacroPass (`Macro/`)

A text pass that runs before lexing, once per file:

- Each file is processed with only the predefined macros plus what its own includes export. A `#define`
  written before an `include` therefore cannot change how the header is expanded - every includer sees the
  same header (the program-model rule).
- After a file is processed its final macro table is merged into every includer, so macros flow down the
  include chain like C headers. Each file is expanded once, however many files reach it (BUG-239).
- `Compiler/SourceMacros` (`ExpandSourceMacros`) runs it over every registered source, a source given as text
  too (known as `memory:<id>`, BUG-237); an interface file of a script's host (docs/runtime/04) is passed through
  as it is, with the macros it exports. The interop pass needs files on disk (it finds headers beside them).
- `Preprocessor` (`MacroPreprocessor.cpp` lines, `MacroDirectives.cpp` directives) reads physical lines,
  tracks comments that span lines, joins `\` continuations and macro calls that continue on later
  lines, and keeps every line in place. `ConditionalStack` holds the `#if` groups, `MacroExpression`
  evaluates conditions, `MacroExpander` / `MacroSubstitution` expand (`#`, `##`, `__FILE__`, `__LINE__`).
- Errors and warnings come back per file (`FileResult::diagnostics`); `CompilerInstance::AddMacroDiagnostics`
  places them at their sources, and `GetAllDiagnostics` lists them first (CLI and LSP alike).
- Predefined: `PredefinedMacros(test, -D defines)` - `TEST`, `PLATFORM_WINDOWS`, `ARCH_X64`, `C4_VERSION`.
- The default library folder is a root of the pass too, so Bedrock may use `#if`.
- `-export-macros` writes each file's final table as `<name>.c4macro`.
- Language-level rules are in [../language/02-preprocessor.md](../language/02-preprocessor.md).

### InteropPass (`Interop/`)

Scans the macro-expanded text for root-level `extern` directives:

```c4
extern "c++" include "game.hpp" { include_dir("third_party"); lib("d3d12.lib"); }
extern "c" source "helpers.c";
```

- `include`: runs `CBind.exe` (`$C4_HOME/CBind/CBind.exe`, else `<dir of C4.exe>/../CBind/CBind.exe`) on the
  header as a subprocess, which uses clang libtooling to generate C4 declarations. The generated text is
  appended to the including file and cached as `<hash>_<name>.c4bind`, keyed on the header and CBind
  timestamps. C++ headers also get a generated shim with an `extern "C"` wrapper for every bound
  constructor, destructor, method and C++-linkage function, compiled and linked as a foreign object, so
  C4 calls them (virtuals dispatch through a typed pointer in the wrapper).
- `source`: compiles the C/C++ file with the bundled clang (`-std=c11` / `-std=c++20`) into
  `%TEMP%/c4_interop`, and links the object.
- `lib("x.lib")`: adds an import library (bare names are resolved against the newest Windows SDK).
- Any interop failure is a hard error. See [../language/13-interop.md](../language/13-interop.md).

The effective text (macro-expanded + bindings) replaces each `Source`'s text, matched by normalized absolute
path. Diagnostics still report original file names.

## 3. Parse (`Compiler/CompilerInstance`)

- `CompileAll` parses every registered source on a pool of `hardware_concurrency` threads. Each file is
  lexed and parsed exactly once per compilation - there is no header re-parsing.
- When a unit reaches `include "x.ch"` it posts a `RequestFileInclude` message (`CompilerMessageQueue`).
  The requester is parked until `x.ch` finishes; then `x.ch`'s transitive include set is folded into the
  requester's `m_IncludesIDS`. The result is a per-unit **transitive include closure**, which is the
  visibility domain used by every later name lookup. Include cycles are detected and recovered.
- Include path resolution (`SourceManager::ResolveIncludePath`): first as a root-relative name, then relative
  to the including file's directory (handles `..`). MacroPass resolves the other way round (relative first,
  then root fallback); both reach the same file for well-formed projects.
- Parser structure: one AST node type per `.h/.cpp` under `Parser/Nodes/`. Each node has a static
  `TryParse(ParserState&)` that commits irrevocably once its keyword matches, then `ParseBody`. Expressions
  use the Pratt parser (`PrattParser`, binding powers in `InfixBindPower`). Speculative parses (template
  argument lists vs comparisons, casts) use `LexerRecovery` save/restore plus `ErrorTracker` suppression
  frames, so a rolled-back probe never leaks diagnostics. Expression and block nesting depth are capped with
  a clean error.

## 4. Analyze (`CompilerInstance::AnalyzeProgram`)

1. `MergeIncludes()` splices included ASTs into each unit's root. This exists for LSP navigation only; the
   model does not use merged trees.
2. `DetectUserMain()` scans entry files for a lowercase global `main`.
3. One `ModelBuilder` runs over every reachable unit's OWN top-level declarations. Each declaration is owned
   by the unit it is written in, so the union of all units yields every definition exactly once - no AST
   merge, no per-file re-analysis. Units with parse errors are skipped (their errors still surface).
4. Builder passes (details in [02-program-model.md](02-program-model.md#build-passes)):
   pass 1 registers every shell; pass 2 resolves to a fixed point (types, bases, fields, packed regions,
   methods, free and extension functions, out-of-line bodies, constant folding); then template
   specializations named inside bodies are instantiated and `TypeStore::FinalizeLayouts` computes layouts.
   The prelude (`Bedrock/Prelude/prelude.ch`, which `SourceManager::RegisterIncludedLibraryFiles` always
   registers) is visited after the program, and every unit's include closure contains it (D-43).
5. Diagnostics from the parse and the model are one list (`GetAllDiagnostics`), reported once by
   `DiagnosticReporter` as `file (line:col) error: message` with source context. Any front-end error stops
   the build before codegen.

## 5. Backend (`Backend/CodeGen/CodeGenDriver::Lower`)

Assembly order (details in [04-codegen-and-abi.md](04-codegen-and-abi.md)):

1. Lower every object/union type to a named LLVM struct, filled from the model's layout.
2. Forward-declare every function (mutual recursion and forward calls are legal before any body).
3. Lower every function body to HIR (`LowerFunction`) **to a fixpoint**: lowering can instantiate
   templates, which adds new function records, so the record set is re-collected until nothing new
   appears. Any lowering error fails the build (never a silent omission); otherwise every complete tree is
   emitted.
4. Under `-test`, synthesize the test-harness `main`.
5. Emit a thin `@main` forwarding to the user's entry (which carries an overload suffix such as `main_0`),
   mirroring `argc/argv` when declared; a `void` entry returns 0.
6. Lower and emit `__c4_global_init` (`Backend/HIR/GlobalInit`), registered in `@llvm.global_ctors`:
   every global and static member that does not fold to a constant starts there, in declaration order, and
   each that needs destruction is registered for exit. It runs in rounds, each emitted with the functions it
   reaches: a template instance first made by a round's code gets its static data members started in the
   next round. It comes after the bodies so the instances they made are started too.
7. Materialize every global (reflection registers each, whether or not code names it), then emit the
   reflection image and `__c4_reflect_init` (`Backend/Reflection/RegistrationEmitter`), a static constructor
   that registers it under the `-reflect-tag` tag before every other one.
8. Emit `__c4_gc_roots`: `{address, size}` of every global, static member and static local that may hold a
   managed pointer.
9. Verify each function (naming the culprit), then the module.

With `-g` the module also carries CodeView debug information: a subprogram per written function, its
parameters and locals, the types, the globals and a line per statement
([04-codegen-and-abi.md](04-codegen-and-abi.md#debug-information--g)).

The `.ll` is always written next to the output (`<out-base>.ll`). `-S` / `--emit-ir` stop after it.

## 6. Object, runtime, reflection, link (`Compiler/BuildUtils`)

- `CompileIRToObj`: `clang -c -target x86_64-pc-windows-msvc -mstackrealign -fexceptions -O<n>`. `-O<n>` is
  the only optimization in the pipeline; C4 itself runs no optimization pass (GEMM at `-O2` measured at
  parity with C++ - see [../tools/05-test-suites.md](../tools/05-test-suites.md)).
- Runtime objects are compiled on demand (`Compiler/RuntimeObjects`) from every `.c` / `.cpp` under the
  install's `C4Runtime/` ([../tools/01-compiler-cli.md](../tools/01-compiler-cli.md#4-the-install)) into
  `%TEMP%/c4_runtime/<hash of the runtime folder>/O2` (at `-O2 -Werror`), so two installs never share
  objects. Each object is named for the bytes of its source and of every runtime header (`GC_GCHeap-<key>.obj`),
  so a changed file builds a new object whatever the files' times say, and the older versions are removed. All
  are linked into every executable: the collector (`GC/`), threads, memory helpers, reflection (`Reflect/`), IO,
  testing, static storage (`StaticStorageRuntimeC.c` + `StaticStorageShim.cpp`: once guards and exit-time
  destruction) and EH (`EHRuntime.cpp`).
- `LinkWithLLD`: `clang -fuse-ld=lld` with the main object, runtimes, interop objects,
  libraries and the `-l` / `-L` arguments, `/SUBSYSTEM:CONSOLE`; with `-g`, `/DEBUG` (the `.pdb` beside the exe).
- clang, lld, the runtime sources, CBind and Bedrock come from the install `C4.exe` runs from
  (`Compiler/Toolchain`, [../tools/01-compiler-cli.md](../tools/01-compiler-cli.md#4-the-install)).

**Planned:** the target is fixed to Windows x64; Linux is B31.

## Include visibility

C4 compiles one program, but name visibility still follows includes. The rules, as enforced by
`SymbolTable::IsVisible` / `FindVisible`:

| Situation | Rule |
|---|---|
| Declaration-level resolution during the build (field types, signatures, bases) | Program-wide (unit 0 sees everything). A header may name a type from a sibling header it never includes; the model is complete regardless of include order. |
| Body-level resolution (HIR lowering) | The defining unit of the symbol must be the asking unit, a builtin (unit 0), or in the asking unit's transitive include closure. |
| Functions | Visibility comes from the record's `declUnit` (where it is DECLARED), not where the body is. Including the header that declares a function is enough, even though the body lives in a `.cf`. |
| Bedrock | Always compiled; visible only through `include "bedrock.ch"` (or a file it reaches). |
| File-scope `static` free functions/variables | Internal linkage: same-named statics in different files are distinct symbols; a lookup prefers the asking unit's own entry. |
| Duplicate non-static global types/variables across files | Hard error (`redefinition of type 'X'`). There are no tentative definitions: a global in a header is defined once because the header is a unit. |
| Order at file scope | Irrelevant for types and functions (no forward declarations needed). Inside function bodies, local declarations register in order. |

**Partial:** const folding is order-independent among const initializers and array sizes, but a file-scope
const used in some expression positions before its declaration fails to fold and is reported against the
consumer (`unresolved signature for function 'main'`) - a weak diagnostic tracked as a bug.

## Phase contracts

| Phase | Input | Output | Must never |
|---|---|---|---|
| MacroPass | file text | expanded text + macro table | depend on the includer's macros |
| InteropPass | expanded text | text with bindings, foreign objects, libs | silently skip a failed binding |
| Parse | effective text | one AST per file, include closure | parse a file twice |
| ModelBuilder | every unit's own top-level declarations | `ProgramModel` | lower a function body |
| HIR | one `FunctionRecord` + the model | typed `HIRFunction` (complete or not) + diagnostics | mutate the AST, re-derive layout |
| CodeGen | HIR trees + model layout/symbols | one verified `llvm::Module` | resolve names, overloads or types |
| BuildUtils | `.ll`, runtimes, foreign objects | `.obj`, `.exe` | hide a tool failure |

The LSP (`C4LSP`) runs phases 1-4 plus HIR lowering for diagnostics, so editor errors are exactly the
compiler's (see [../tools/03-editor-lsp-dap.md](../tools/03-editor-lsp-dap.md)).
