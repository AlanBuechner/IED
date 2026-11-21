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
        |   MacroPass    : #define/#if/... over tokens, macro tables inherited down includes
        |                  -> each file's MacroLayout (left-out lines + expansions); its text is never rewritten
        |   InteropPass  : extern "c"/"c++" include|source -> CBind bindings, foreign objects, libs
        |                  -> each header's bindings a unit of their own, which the binding file includes
        v
 [3] CompileAll (CompilerInstance) -- parallel parse, each file exactly once
        |   the Lexer reads the file's own text through its MacroLayout
        |   include "x.ch" (or a binding unit) -> RequestFileInclude message, requester parks until x is
        |   parsed, x's include closure is folded into the requester's closure
        v
 [4] AnalyzeProgram
        |   DetectUserMain
        |   ModelBuilder: pass 1 REGISTER shells -> pass 2 RESOLVE fixed point -> FinalizeLayouts
        |   -> one ProgramModel; diagnostics = parse errors + model errors
        |   any front-end error stops here
        v
 [5] RunBackend: ProgramBackend::Lower
        |   ProgramLowering: written bodies -> what they need (D-16), to a fixpoint -> reflection's thunks
        |     -> global starts, round by round -> thunks -> every global's storage (the same for C4LSP)
        |   ProgramEmitter: types -> function decls -> every lowered tree -> test harness or @main wrapper
        |     -> __c4_global_init rounds -> globals -> reflection image -> __c4_gc_roots -> verify
        v
     -S / --emit-ir writes <out>.ll and stops here; -bc writes <out>.bc and stops
        |
        v
 [6] OptimizeModule, then WriteCachedObjects (in the compiler): LLVM's -O<n> pipeline, the module split into
     partitions, and the X86 backend on -j threads for each partition whose object is not already cached
     -> <int>/<out name>.objects/<hash>.obj (-c: <out>.obj)
 [7] runtime objects (GC, reflect, test, IO as C; EH as C++), cached in %TEMP%/c4_runtime
 [8] lld-link: main obj + runtimes + foreign objs + libs -> <out>.exe
```

`-ftime` prints the wall-clock time of each phase: register (reading the sources, on several threads), preprocess,
parse, model, lower, script host (with
`--script-host`), optimize and codegen (one `optimize+codegen` phase in an incremental build, which writes each
partition's bitcode and assembles the partitions it changed on the same threads), inline bodies (with `--script-host`),
interface, runtime+link.

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

A pass over each file before it is parsed, once per file, which never rewrites the file's text (R8): it lays what it
does over the text as a `MacroLayout` (`Macro/MacroLayout`) - the bytes it leaves out (directive lines, the code of
inactive `#if` groups) and each macro use it expands, with the tokens the use expands to - and the lexer reads the file
through it.

- Each file is processed with only the predefined macros plus what its own includes export. A `#define`
  written before an `include` therefore cannot change how the header is expanded - every includer sees the
  same header (the program-model rule).
- After a file is processed its final macro table is merged into every includer, so macros flow down the
  include chain like C headers. Each file is expanded once, however many files reach it (BUG-239).
- `Compiler/SourceMacros` (`ExpandSourceMacros`) runs it over every registered source, a source given as text
  too (known as `memory:<id>`, BUG-237); an interface file of a script's host (docs/runtime/04) is passed through
  as it is, with the macros it exports. The interop pass needs files on disk (it finds headers beside them).
- `Preprocessor` (`MacroPreprocessor.cpp` lines and uses, `MacroDirectives.cpp` directives) reads physical lines,
  tracks comments that span lines, joins `\` continuations, and finds each use in live code: an object-like name, or
  a function-like name and its call (which may go on to later lines, not past a directive line). The use is read as
  tokens (`Macro/MacroToken`: each with its spelling, the place the file spells it, and whether white space came
  before it), `MacroExpander` / `MacroSubstitution` expand them over tokens (`#`, `##` with C's placemarker,
  `__FILE__`, `__LINE__`; an argument's token keeps its place in the file, a body's or a pasted one has none), and
  the result is one `MacroExpansion` of the layout: the use's bytes, the macro and where it is defined, the tokens'
  text, and each token's place. A use that expands to itself is no expansion. `ConditionalStack` holds the `#if`
  groups, `MacroExpression` evaluates conditions.
- `ApplyMacroLayouts` gives each registered source its layout (`SourceInfo::macros`); the source keeps its text.
  `MacroLayout::Render` gives the text as the parse reads it - left-out bytes become their line breaks, a use its
  expansion then its line breaks - for what needs it as text: the interop pass, a script host's interface file
  (`Script/InterfaceText`, cutting bodies at `RenderedOffset`s), tools.
- Errors and warnings come back per file (`FileResult::diagnostics`); `CompilerInstance::AddMacroDiagnostics`
  places them at their sources, and `GetAllDiagnostics` lists them first (CLI and LSP alike).
- Predefined: `PredefinedMacros(test, -D defines)` - `TEST`, `PLATFORM_WINDOWS` or `PLATFORM_LINUX` (the target's system, D-86), `ARCH_X64`, `C4_VERSION`.
- The default library folder is a root of the pass too, so Bedrock may use `#if`.
- `-export-macros` writes each file's final table as `<name>.c4macro`.
- Language-level rules are in [../language/02-preprocessor.md](../language/02-preprocessor.md).

### InteropPass (`Interop/`)

Reads each file that may hold one (`Interop/InteropSources` `InteropInputsOf`: its text or an expansion has `extern`)
as its parse will - through its layout - for root-level `extern` directives:

```c4
extern "c++" include "game.hpp" { include_dir("third_party"); lib("d3d12.lib"); }
extern "c" source "helpers.c";
```

- `include`: runs `CBind.exe` (`$C4_HOME/CBind/CBind.exe`, else `<dir of C4.exe>/../CBind/CBind.exe`) on the
  header as a subprocess, which uses clang libtooling to generate C4 declarations, cached as
  `<hash>_<name>.<flags>.c4bind` (one per way of parsing the header), keyed on the header and CBind timestamps. The
  bindings are a unit of their own (R8): `RegisterBindings` registers the `.c4bind` once (`SourceManager::
  RegisterBinding`, `SourceInfo::binding`; display name `<header>.c4bind`), and each file that binds the header lists
  it (`SourceInfo::bindings`) and includes it - `CompilationUnit::IncludeBindings` asks for it by its absolute path
  before the file's parse, as a header is asked for, so the file and its includers see the bindings and the file's
  own text and lines are untouched. C++ headers also get a generated shim with an `extern "C"` wrapper for every bound
  constructor, destructor, method and C++-linkage function, compiled and linked as a foreign object, so
  C4 calls them (virtuals dispatch through a typed pointer in the wrapper).
- `source`: compiles the C/C++ file with the bundled clang (`-std=c11` / `-std=c++20`, the build's `-O` and `-g`) and
  links the object, which is reused while a key of its source, the headers its depfile (`-MD`) lists, its flags and
  clang is unchanged (`Interop/ForeignCache`). The
  bindings, shims and objects go to the build's own `<int>/<output name>.interop/` folder (`--int-dir`, else beside
  the output), each named after a hash of its file's absolute path and the file's name: two programs built at
  once, two same-named files in different folders, and `util.c` beside `util.cpp` never share an output.
- `lib("x.lib")`: adds an import library (bare names are resolved against the newest Windows SDK).
- Any interop failure is a hard error. See [../language/13-interop.md](../language/13-interop.md).

Each `Source` keeps the file's text: a location is always the file's own line and column (BUG-446 was the columns
of the expanded text that used to replace it). A token a macro use made is located where the file spells it (an
argument's token) or at the use, and knows its expansion (`SourceLocation::expansion`, `inExpansion`); a
diagnostic there adds `note: in the expansion of the macro 'X' (file:line): ...` (`DiagnosticReporter`).

## 3. Parse (`Compiler/CompilerInstance`)

- `CompileAll` parses every registered source on a pool of `hardware_concurrency` threads. Each file is
  lexed and parsed exactly once per compilation - there is no header re-parsing.
- When a unit reaches `include "x.ch"` it posts a `RequestFileInclude` message (`CompilerMessageQueue`).
  The requester is parked until `x.ch` finishes; then `x.ch`'s transitive include set is folded into the
  requester's `m_IncludesIDS`. The result is a per-unit **transitive include closure**, which is the
  visibility domain used by every later name lookup. Include cycles are detected and recovered.
- A compile given the parses of the one before (`UsePriorParses`, `Compiler/PriorParses`; the language server's
  analysis after an edit, never C4.exe) takes the tree and the lexer's and parser's errors of a file registered with the
  same id that reads the same - its text, its macro layout and its binding units - instead of parsing it, and asks for
  the file's includes (its binding units among them) again
  in the order its parse did (`CompilationUnit::StartReplay`): the include closure and the include errors are what a
  parse would give now, so a file that appeared answers an include, and an error at the place of the parse's error
  just before is dropped as `ErrorTracker::ReportOncePerLocation` drops it. Trees are never changed once parsed, so two
  compiles share them; a compile that takes prior parses never merges includes into its trees (`MergeIncludes`).
- Include path resolution (`SourceManager::ResolveInclude`): beside the including file, then its project's folders,
  its dependencies' and the default library ([../language/15-projects.md](../language/15-projects.md)); a binding
  unit by its absolute path. The macro pass resolves an include the same way (`Compiler/SourceMacros`), so it carries
  the macros of the file the parse includes.
- The lexer (`Lexer`) reads the file's own text through its `MacroLayout`: it passes over what the layout leaves out,
  and reads a use as its expansion's text, locating each token as above. Two tokens form a compound operator only when
  nothing came between them (`Token::FollowsDirectly`): white space at a use's edge keeps them apart as in the text.
- Parser structure: one AST node type per `.h/.cpp` under `Parser/Nodes/`. Each node has a static
  `TryParse(ParserState&)` that commits irrevocably once its keyword matches, then `ParseBody`. Expressions
  use the Pratt parser (`PrattParser`, binding powers in `InfixBindPower`). Speculative parses (template
  argument lists vs comparisons, casts) use `LexerRecovery` save/restore plus `ErrorTracker` suppression
  frames, so a rolled-back probe never leaks diagnostics. Expression and block nesting depth are capped with
  a clean error.
- Error recovery (R7): a file's parse never stops at an error, and one mistake is one error.
  - Before the parse, `Lexer::RecoverBraces` reads the file's tokens and, only when its `{` and `}` do not pair up,
    mends them by the indentation of its lines (`Lexer/BraceRecovery`, `PlanBraceRepairs`): a block never closed is
    closed before the first later line indented no deeper than its own (a `}` that starts a line less indented than an
    open block closes it first), and a `}` indented deeper than the `{` it would close is left out while there are too
    many - each with one error at the brace. A brace forgotten while typing does not swallow the rest of the file.
  - A declaration (file or namespace), member (type body) or statement (function body) that does not parse is one
    error, then `Parser/Recovery`'s `SkipBroken` skips it: past its `;`, past its `{ ... }` group, up to the `}` that
    closes the enclosing body, or up to a line that starts the next one of its kind (`struct`, `public`, `return`...).
    An `ErrorNode` (`Parser/Nodes/ErrorNode`) stands where it was: a namespace's child, a type's child (never one of
    its members), a block's statement. Tokens a declaration leaves after reporting its own error are skipped without
    another (`ErrorTracker::Now` / `ReportedSince`), and an operand that is missing is said once (`PrattParser::
    ParseOperand`: `v *;` is not also "expected expression after '*'").
  - So every declaration that parsed is in the tree, and the model registers it: a header with a syntax error keeps
    its other declarations, and the files that include it report nothing more.

## 4. Analyze (`CompilerInstance::AnalyzeProgram`)

1. `DetectUserMain()` scans entry files for a lowercase global `main`.
2. One `ModelBuilder` runs over every reachable unit's OWN top-level declarations. Each declaration is owned
   by the unit it is written in, so the union of all units yields every definition exactly once - no AST
   merge, no per-file re-analysis. A unit with parse errors is built like any other: every declaration it parsed
   is registered and its `ErrorNode`s are passed by, so its includers resolve what they name in it (R7).
3. Builder passes (details in [02-program-model.md](02-program-model.md#build-passes)):
   pass 1 registers every shell; pass 2 resolves to a fixed point (types, bases, fields, packed regions,
   methods, free and extension functions, out-of-line bodies, constant folding); then template
   specializations named inside bodies are instantiated and `TypeStore::FinalizeLayouts` computes layouts.
   The prelude (`Bedrock/Prelude/prelude.ch`, which `SourceManager::RegisterIncludedLibraryFiles` always
   registers) is visited after the program, and every unit's include closure contains it (D-43).
4. Diagnostics from the parse and the model are one list (`GetAllDiagnostics`), reported once by
   `DiagnosticReporter` as `file (line:col) error: message` with source context. Any front-end error stops
   the build before codegen.

`CompilerInstance::MergeIncludes()` splices included ASTs into each unit's root, for what walks a document's
tree to a declaration in a header it includes (the language server, `--pretty-print`). The model and the
backend never use merged trees, so a build does not merge.

## 5. Backend (`Backend/ProgramBackend`: `Backend/Lowering/ProgramLowering`, then `Backend/CodeGen/ProgramEmitter`)

The backend is two steps. **Lowering** (`ProgramLowering::Run`, [03-hir.md](03-hir.md#what-a-program-lowers-programlowering))
decides what the program has and lowers it to HIR, on `-j` threads; the language server runs exactly this step, so
its body diagnostics are the build's. **Emission** (`ProgramEmitter::Emit`) emits what lowering produced (a
`LoweredProgram`) and decides nothing about what is lowered. Any lowering error fails the build before emission
(never a silent omission), after every lowering step has run, so one run reports every error.

Lowering, in order:

1. Every function written as it is with a body (`LowerFunction`), then what the lowered bodies need, to a fixpoint
   (`ProgramReach`, D-16): what their emitted code calls, takes the address of, constructs, finalizes, destroys at
   exit or copies and destroys exceptions with, and the virtual methods of the objects they hold (`VtableEntries`).
   A member of a class-template instance nothing needs is never lowered.
2. With `--script-host`, every class's vtables.
3. Reflection's invoke thunks and type thunks, and what they need.
4. The starts of the globals and static members (`Backend/HIR/GlobalInit`), round by round, each with what it
   reaches: a template instance first made by a round's code gets its static data members started in the next
   round. They come after the bodies so the instances the bodies made are started too; then the thunks again.
5. What every global's storage needs: its type's vtables, the function a function pointer starts at
   (`GlobalStorage`); again from step 4 while that lowers more.

Emission, in order (details in [04-codegen-and-abi.md](04-codegen-and-abi.md)):

1. Lower every object/union type to a named LLVM struct, filled from the model's layout.
2. Forward-declare every function (mutual recursion and forward calls are legal before any body).
3. Every lowered tree, each in its partition, the partitions at once.
4. Under `-test`, synthesize the test-harness `main`.
5. Emit a thin `@main` forwarding to the user's entry (which carries an overload suffix such as `main_0`),
   mirroring `argc/argv` when declared; a `void` entry returns 0.
6. Emit `__c4_global_init`'s rounds, registered in `@llvm.global_ctors`: every global and static member that does
   not fold to a constant starts there, in declaration order, and each that needs destruction is registered for
   exit. A start that only stores constants (a table) becomes the global's initial data instead (`ConstantImage`),
   so a program whose globals are all such starts has no rounds.
7. Materialize every global (reflection registers each, whether or not code names it), then describe the
   reflection image from the model (`Backend/Reflection/RegistrationEmitter`) and emit `__c4_reflect_init`, a
   static constructor that registers it under the `-reflect-tag` tag before every other one. The image's constants
   (`Backend/Reflection/ReflectionTables`) go into the module; a split program's go into a module of their own,
   built only when the object cache keeps no object of the image's key (D-82,
   [05-partitioned-emission.md](05-partitioned-emission.md#the-reflection-image-d-82)).
8. Check that the emitted code names no function with a body that lowering did not lower (an internal error
   otherwise: lowering and codegen would disagree about what the program needs).
9. Emit `__c4_gc_roots`: `{address, size}` of every global, static member and static local that may hold a
   managed pointer.
10. Verify each function (naming the culprit), then the module.

With `-g` the module also carries CodeView debug information: a subprogram per written function, its
parameters and locals, the types, the globals and a line per statement
([04-codegen-and-abi.md](04-codegen-and-abi.md#debug-information--g)).

`-S` / `--emit-ir` write the module as `<out-base>.ll` and stop; a normal build writes no `.ll` (the object comes
straight from the module in memory).

## 6. Object, runtime, reflection, link (`src/ProgramOutput`, `Compiler/ObjectEmission`, `Compiler/BuildUtils`)

- `OptimizeModule` and `WriteCachedObjects` (roadmap 5.1, 5.3): the module is optimized and assembled inside the compiler, with no textual-IR round
  trip or second process. A `TargetMachine` for the build's CPU and features runs LLVM's default pipeline at
  `-O<n>` (`PassBuilder::buildPerModuleDefaultPipeline`, `buildO0DefaultPipeline` at `-O0`; loop unrolling,
  interleaving and both vectorizers from `-O2`, as clang sets them), then the X86 backend writes the COFF object.
  `-O<n>` is the only optimization in the pipeline; C4 itself runs no optimization pass (GEMM at `-O2` measured at
  parity with C++ - see [../tools/05-test-suites.md](../tools/05-test-suites.md)). An error the assembler reports
  for an `asm` block comes back with the block's `!srcloc` cookie (`AsmEmitter::LocationCookie`) and is reported at
  the C4 line: `main.cf (4:2) error: invalid operand for instruction: 'mov rax, ecx'`.
- Partitions (`Compiler/ModulePartitions`): after the whole module is optimized (inlining across the program is
  unchanged), it is split into partitions that link back into the same program. Where a definition goes
  (`Compiler/PartitionPlacement`): data no source file names (the reflection image and its rows, the GC roots table)
  in the first partition, alone, so an edit that changes it reassembles no code with it (the IED server's reflection
  is 13 MB of data); anything else in the others: with debug info, by the source file it names, so a file's code
  shares a partition (a large file is spread by name over one partition per 8,000 instructions, rounded down to a
  power of two) and an edit that moves a file's lines, which its debug info records, changes only that file's
  partitions and those that copy its small functions; without debug info, by its name. A comdat's members stay together (by the comdat's name
  and its leader's file); private data goes with the user whose name sorts first, so a literal travels with its
  function. Each partition is built from its own definitions plus declarations of only what they reach, so it changes
  only when its own code does; its debug-info compile unit lists only the globals it defines. Local symbols become
  hidden globals with a `.local` suffix, which no C or C++ symbol can have, so they meet no foreign object's names in
  the link; file-scope assembly and the compile unit's program-wide lists (enums, retained types, imports) stay in the
  first partition. How many: `--partitions <n>`, else about one per 1,000 instructions (3,000 at `-O0`,
  where an instruction is cheaper to assemble) rounded down to a power of two, from 1 to 64, so the count changes
  only when the program doubles or halves. Partitions are built and assembled on up to `-j` threads (default: the
  processors): the module is written once as bitcode as the program made it, each thread reads a copy of it
  lazily into a context of its own, builds what else was asked of the split from it (a script host's inline bodies,
  `SplitModuleAndExtract`), then makes its locals hidden globals and builds its share of the partitions (a definition
  found by its place in the module's lists), so a partition's bitcode is the same whatever the thread count. `-c`
  always writes one object. `-ftime` shows the split as a phase of its own and a line on the partitions: how many
  were reused, and of those assembled the longest, the total and the largest.
- The object cache (`Compiler/ObjectCache`, roadmap 5.3): a linked build keeps its objects in
  `<int>/<out name>.objects/`, each named after the hash of its partition's bitcode and of the compiler (its version,
  and the size and time of its executable) and the target settings, so a partition whose code is unchanged reuses
  its object and a rebuilt compiler or another `-O` / `-march` reuses none. A split program's reflection image is kept
  by a key of its own, the hash of what it is made of, and built only when that object is missing (D-82). Partitions
  with the same code (empty ones, in a small program split into many) share one object, assembled once and linked
  once. An object is written beside its name and moved into place when complete; the folder keeps only the latest
  build's objects. `-v` says how many were reused. Two compilers can build one project at once (two IED chats, each
  from its own sources): each claims the objects it names, before it looks for them, in the folder's `.claim.<pid>`, and
  a build's pruning spares what a running compiler claimed and its unfinished writes, and removes an ended one's claims;
  an object pruned in the moment before a claim was there to read is assembled again before the link (BUG-442, BUG-443).
  The cache's files are handled with LLVM's file calls, which take paths longer than `MAX_PATH` (a deep build folder);
  the interop pass's and CBind's files are too (`Compiler/FileIo`).
  Names do not depend on the order code is generated in (`Backend/CodeGen/StableNames`: literals, test-runner strings
  and reflection tables are named after their content; exception and RTTI records after the type they describe), so
  an edit renames nothing it did not change. On the Bedrock tests at `-O0` (32 partitions): a cold build 0.96 s, an
  unchanged rebuild 0.41 s, adding a test 0.46 s (28 objects reused), editing one 0.43 s (31 reused).
- Incremental builds (`--incremental`, D-76, roadmap 5.3): the program is emitted straight into its partitions, on
  as many threads as `-j` allows ([05-partitioned-emission.md](05-partitioned-emission.md), D-81), and each partition
  is optimized alone, on its own thread, so an unchanged partition skips the optimizer as well as the assembler. A
  partition carries `available_externally` copies of the small functions its code calls from other partitions (up
  to 100 instructions of unoptimized IR for a direct callee, 70% of that for each level further, three levels, as
  ThinLTO imports; nothing for `minsize` callers such as reflection's invoke thunks, nothing `noinline`), so they
  still inline; its bitcode, and with it the cache key, includes those copies. At `-O0` only the always-inliner and
  the aggregate copies run, so a partition copies only the `inline` functions it calls. Lowering leaves the
  partitions unverified and each is verified as it is assembled; a script host's inline bodies are extracted per
  partition. A program of one partition (under 48 functions) takes the module path above, with the split and
  `Compiler/PartitionImports`. Linkonce definitions become weak in this mode, since a partition's optimizer would drop one that only another
  partition uses. The cache salt says `incremental`, so its objects never mix with a whole-program build's. On the
  60k-line ScaleTest at `-O2` (32 partitions, `-j 3`): a cold build 9.1 s (the whole-program build: 12.5 s), an
  unchanged rebuild 2.0 s (front end 1.1 s, split and hash 0.8 s), a one-module edit 2.6 s (31 objects reused); the
  program runs at the whole-program build's speed, and the math benchmarks within 0-20% except a kernel whose helpers
  are too large to copy (`batch_pointers`, 60%). With `-g` (Nuke's Development), on the IED server's sources at
  `-O3` (32 partitions, 4 threads): a cold build 10.4 s, an unchanged rebuild 2.3 s, a blank line atop a 660-line
  file 4.3 s (25 objects reused; before file placement, BUG-328, 10.1 s and 5).
- The X86 target is registered with its `LLVMInitializeX86*` functions; the compiler links LLVM statically and must
  not link `LLVM-C.lib`, whose import of `LLVM-C.dll` would bind those functions to the DLL's own copy of LLVM.
- Runtime objects are compiled on demand (`Compiler/RuntimeObjects`) from every `.c` / `.cpp` under the
  install's `C4Runtime/` ([../tools/01-compiler-cli.md](../tools/01-compiler-cli.md#4-the-install)) into
  `%TEMP%/c4_runtime/<hash of the runtime folder>/<the target's folder>` (`O2`, `linux-x64-O2`; at `-O2 -Werror`), so two installs never share
  objects. Each object is named for the bytes of its source and of every runtime header (`GC_GCHeap-<key>.obj`),
  so a changed file builds a new object whatever the files' times say, and the older versions are removed. All
  are linked into every executable: the collector (`GC/`), threads, memory helpers, reflection (`Reflect/`), IO,
  testing, static storage (`StaticStorageRuntimeC.c` + `StaticStorageShim.cpp`: once guards and exit-time
  destruction) and EH (`EHRuntime.cpp`).
- The link is the target's (`TargetAbi::Link`, [04](04-codegen-and-abi.md#targets-targetabi)). On Windows: `lld-link`
  with the main object, runtimes, interop objects, libraries and the `-l` / `-L` /
  `-Wl,` arguments as clang's driver would hand them over (`LinkerArguments`), `libcmt`, `oldnames` and clang's
  runtime libraries, `/SUBSYSTEM:CONSOLE`; with `-g`, `/DEBUG` (the `.pdb` beside the exe). lld-link takes the MSVC
  and Windows SDK libraries from `setup.bat`'s kit (`Toolchain::WindowsSysroot`, `/winsysroot`), else finds an
  installed Visual Studio's itself (or through `LIB` when it is set), so no clang process starts for the link. On
  Linux (D-86): clang's g++ driver with ld.lld, a PIE against the Linux sysroot's glibc, the runtime's objects linked
  only when used (`--start-lib`).
- clang, lld, the runtime sources, CBind and Bedrock come from the install `C4.exe` runs from
  (`Compiler/Toolchain`, [../tools/01-compiler-cli.md](../tools/01-compiler-cli.md#4-the-install)).

The target is Windows x64 or Linux x64 (`--target`, D-86); another one is a `TargetAbi` implementation (R11).

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
| MacroPass | file text | a MacroLayout (left-out lines, expansions over tokens) + macro table | depend on the includer's macros; rewrite the text |
| InteropPass | each file's text through its layout | binding units, foreign objects, libs | silently skip a failed binding; touch the binding file's text |
| Parse | each file's text through its MacroLayout, binding units as includes | one AST per file, include closure | parse a file twice, rewrite a file's text |
| ModelBuilder | every unit's own top-level declarations | `ProgramModel` | lower a function body |
| HIR | one `FunctionRecord` + the model | typed `HIRFunction` (complete or not) + diagnostics | mutate the AST, re-derive layout |
| Lowering (`ProgramLowering`) | the model | every tree the program needs, in one order (`LoweredProgram`) + diagnostics | depend on emission or on the threads |
| CodeGen (`ProgramEmitter`) | a `LoweredProgram` + model layout/symbols | verified `llvm::Module`s | resolve names, overloads or types; decide what is lowered |
| ProgramOutput / ObjectEmission / BuildUtils | the verified module, runtimes, foreign objects | `.obj`, `.exe` (`.ll` for `-S`, `.bc` for `-bc`) | hide a tool failure |

The LSP (`C4LSP`) runs phases 1-4 plus the backend's lowering step (`ProgramLowering`, no emission) for
diagnostics, so editor errors are exactly the compiler's (see [../tools/03-editor-lsp-dap.md](../tools/03-editor-lsp-dap.md)).
