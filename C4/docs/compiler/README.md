# Compiler internals

These pages describe how the C4 compiler (`C4.exe`, built from `src/` + `C4Lib/`) is put together today and
the architecture rules every change must respect. They are for people working ON the compiler. For the
language itself see [../language/](../language/), for the runtime see [../runtime/](../runtime/), and for
building and testing the compiler see [../tools/04-building-the-compiler.md](../tools/04-building-the-compiler.md)
and [../tools/05-test-suites.md](../tools/05-test-suites.md).

| Page | Contents |
|---|---|
| [01-pipeline.md](01-pipeline.md) | End-to-end pipeline: driver, macro + interop passes, parallel parse, program model, HIR, codegen, clang, runtime, link; include visibility |
| [02-program-model.md](02-program-model.md) | `ProgramModel`: data structure, query verbs, scopes, templates, overloads, const eval, OperatorTable |
| [03-hir.md](03-hir.md) | HIR contract, node taxonomy, lvalues/values, lowering walks, RAII scopes, diagnostics |
| [04-codegen-and-abi.md](04-codegen-and-abi.md) | LLVM emission design, Windows x64 ABI, vtables, EH funclets, GC emission, test harness, entry point, LLVM traps |
| [05-partitioned-emission.md](05-partitioned-emission.md) | How an incremental build emits the program into its partitions on all threads (D-81): the coordinator, placement, symbols across partitions, imports, a script host |

## Architecture principles (non-negotiable)

These rules came out of a long bug history in which every major defect had the same root cause: a later
phase re-derived a semantic fact an earlier phase already knew, and the two answers drifted. The current
design gives every question exactly one home.

1. **One model, one API.** Every phase (HIR lowering, codegen, LSP, future DAP) holds ONE `ProgramModel`
   and asks it questions through a small set of query verbs (`ResolveType`, `QueryType`, `QueryFunction`,
   `QueryOperator`, `QueryVariable`, `QueryMember`, `QueryConstant`, `SizeOf`, ...). There is no second bag
   of facts, no node-keyed side maps, no phase-local type caches that re-answer semantic questions.
2. **Single source of truth.** A semantic fact (type, size, field offset, visibility, constness, which
   operator method applies) is computed once and carried as data. HIR nodes carry concrete types; codegen
   never reasons about semantics - it emits what the node says.
3. **The model does not lower function bodies.** Declarations are the model's job; bodies are lowered by
   HIR on demand, which is also where all body-level validation happens. Broken code therefore produces
   diagnostics, never crashes. (One deliberate exception: after resolution, `ModelResolver::
   InstantiateBodyReferences` walks bodies once only to instantiate the template specializations they
   name, so those types are concrete before any lower system runs.)
4. **Implicit handling inside queries.** Callers never manage templates, overloads or visibility:
   - templates instantiate on first use inside `ResolveType` / `QueryFunction` and are cached by value;
   - overloads, operators and constructors resolve inside `QueryFunction` / `QueryOperator` through one
     ranking mechanism (`OverloadResolver`), default arguments included;
   - include-based visibility is enforced inside every name lookup from the caller's `LookupContext`.
5. **Errors are values.** Queries return `QueryResult<T>` - a value, or a `QueryError` kind plus an
   actionable message and location. No exceptions, no "nullptr means error", no silent fallback.
6. **One operator catalog.** `Model/OperatorTable` is the only token -> canonical-name map. The model,
   HIR and the annotation collector all read it, so they cannot disagree about what `-` or `[]` means.
7. **Honest functions, no global mutable state.** A `CompilerInstance` owns all data for one program;
   phases receive what they need as parameters (the model, the error tracker). Lowering contexts are
   per-function and transient. Nothing process-global is mutated during compilation.
8. **One brick per concept.** The moment two call sites need the same answer (calling convention,
   pointer adjustment, truthiness, GC-correct store), it becomes one named function and both call it.
   Examples: `Abi` for calling convention, `ExprEmitter::GcStore` for barrier-guarded stores,
   `ObjectType::GetPointerAdjustment` for base-subobject deltas, `LoweringContext::LoadIfNeeded` for
   lvalue-to-rvalue.
9. **Files are 1:1 `.h`/`.cpp` pairs of at most ~600 lines**, organized by subsystem. A class too large for
   one file splits its member definitions by concern into several `.cpp` files behind one header (the
   expression lowerer into `ExprLowerer`, `ExprAccess`, `ExprCalls`, `ExprCallTargets`, `ExprConstruction`,
   `ExprOperators`, `ExprCasts` and `ExprAssignment`; the Pratt parser into `PrattParser`, `PrattPrefix`,
   `PrattInfix`, `PrattPostfix` and `PrattCastsAndFolds`; the model builder into `ModelBuilder`,
   `ModelBuilderTypes`, `ModelBuilderValues` and `ModelBuilderUnions`; `Lifecycle` and `LifecycleParts`;
   `DeclLowerer` and `DeclBraces`; `ExprEmitter` and `ExprOperatorEmitter`).
10. **Fix the architecture, not the symptom.** A bug that comes from a phase answering a question it does
    not own is fixed by moving the answer to its owner, not by patching the call site.

## Source tree (`C4Lib/src`)

`C4Lib` is a static library; `src/Source.cpp` is the thin `C4.exe` driver on top of it. `C4LSP` links the same
library so the editor sees exactly what the compiler sees.

| Folder | Responsibility |
|---|---|
| `SourceManager/` | `Source` (one file's text + id) and `SourceManager` (registration of files/folders, include-path resolution, effective-text overrides after the macro/interop passes) |
| `Macro/` | Text pre-pass before lexing: `MacroPreprocessor` (directives, `#if` evaluation), `MacroExpander` (object/function-like + `__VA_ARGS__`), `MacroTable`, `MacroPass` (runs every file once, inherits macro tables down the include chain) |
| `Interop/` | `InteropPass`: finds `extern "c"/"c++" include|source` directives, runs CBind to generate C4 declarations, compiles foreign sources, collects libs and foreign objects |
| `Lexer/` | `Token` (kinds, keyword promotion), `Lexer` (atomic tokens; compound operators formed on request, template-aware `>>` splitting), `TokenArena`, `LexerRecovery` (save/restore for speculative parses) |
| `Parser/` | `PrattParser` (expressions, binding powers), `ParserState`, `AstNode`; `Nodes/Declarations`, `Nodes/Expressions`, `Nodes/Statements` (one node type per `.h/.cpp`, `TryParse` + `ParseBody` pattern); `Consumer/` (AST walker, pretty printer) |
| `Compiler/` | `CompilerInstance` (owns one compilation: units, parallel parse scheduler, include closures, the model), `CompilationUnit`, `CompilerMessageQueue`, `ErrorTracker` (diagnostics + speculative-parse suppression frames), `DiagnosticReporter` (rendering), `BuildUtils` (CLI parsing, clang/lld invocation, runtime object build) |
| `Model/` | The `ProgramModel` and its subsystems: `SymbolTable` (scope tree), `TypeStore` (owns every type), `TypeResolver`, `SymbolQueries`, `TemplateInstantiator`, `OverloadResolution`, `OperatorTable` + `OperatorAnnotations`, `ConstEvaluator`, `LoweringScopes`, `TypeHandle`, `QueryResult`, reflection C emission (`ProgramModel.cpp`) |
| `Model/Builder/` | `ModelBuilder` (pass 1 register + orchestration), `ModelResolver` (pass 2 fixed point), `FreeFunctionResolver` (free functions, extension methods, out-of-line definitions) |
| `Model/Types/` | One class per type kind: Primitive, Pointer, Reference, Array, FunPtr, Function, Object, Union, Enum, Alias, Swizzle (packed sub-vector), InitList, TemplateParam, TemplateSpecialization, Invalid |
| `Backend/HIR/` | Typed lowering of function bodies: `HIRNodes`, `HIRValue`, `ModelQueries`, `LoweringContext`, `ExprLowerer`, `StmtLowerer`, `DeclLowerer`, `LoopLowerer`, `ExceptionLowerer`, `JumpScopes`, `IndexLValue`, `LoweringDriver` |
| `Backend/CodeGen/` | LLVM emission from HIR: `ModuleContext`, `IRContext`, `CleanupStack`, `Abi`, `ExprEmitter`, `RegionEmitter`, `FunctionEmitter`, `CodeGenDriver` |
| `Testing/` | Empty (test-mode support lives in the model's test registry and `CodeGenDriver`'s harness) |

Runtime C sources linked into every program live outside the library in `C4Runtime/` (see
[../runtime/01-memory-and-gc.md](../runtime/01-memory-and-gc.md)).

## Legacy doc-number references

Many source comments cite design documents by number ("doc 04", "doc 06", "plan 14", "spec 5.8"). Those
documents were replaced by this docs set. Read the reference according to the folder the comment is in:

| Comment in | Old reference | Old title | Now covered by |
|---|---|---|---|
| `Model/*` | model doc 01 | Why: what is wrong with the old semantic layer | [README principles](#architecture-principles-non-negotiable) |
| `Model/*` | model doc 02 | The pipeline (pass 1 register, pass 2 resolve) | [02-program-model.md, Build passes](02-program-model.md#build-passes) |
| `Model/*`, `Compiler/*` | model doc 03 | The data structure: one object owns everything | [02-program-model.md, Data structure](02-program-model.md#data-structure) |
| `Model/*`, `Backend/HIR/*` | model doc 04 | The API: query verbs | [02-program-model.md, Query API](02-program-model.md#query-api) |
| `Model/*` | model doc 05 | Templates | [02-program-model.md, Templates](02-program-model.md#templates) |
| `Model/*`, `Backend/HIR/*` | model doc 06 | Scopes, RAII layers, local declarations | [02-program-model.md, Lowering-time scopes](02-program-model.md#lowering-time-scopes) |
| `Model/*` | model doc 08 | Overloads | [02-program-model.md, Overload resolution](02-program-model.md#overload-resolution) |
| `Model/*` | model doc 09 / arch doc 09 | Complex cases / single program model and include visibility | [01-pipeline.md, Include visibility](01-pipeline.md#include-visibility) |
| `Model/*` | model doc 10 | Template body substitution: rejected | [02-program-model.md, Templates](02-program-model.md#templates) (fake aliases, no substitution engine) |
| `Model/*` | arch doc 06 / 10 | Const eval design / on-demand const eval | [02-program-model.md, Constant evaluation](02-program-model.md#constant-evaluation) |
| `Model/*` | arch doc 08 | Vector types (packed views) | [../language/11-vectors-and-swizzling.md](../language/11-vectors-and-swizzling.md), [02-program-model.md](02-program-model.md#data-structure) |
| `Backend/HIR/*` | hir doc 03 (03.2, 03.4, ...) | The HIR design (values, context, nodes) | [03-hir.md](03-hir.md) |
| `Backend/HIR/*` | hir doc 04 (04.1) | Scoping, shadowing, template bindings, labels | [03-hir.md, Scopes and labels](03-hir.md#scopes-and-labels) |
| `Backend/HIR/*` | hir doc 05 | Feature matrix: AST -> lowering rule | [03-hir.md, Lowering rules](03-hir.md#lowering-rules) |
| `Backend/HIR/*` | hir doc 06 | Diagnostics | [03-hir.md, Diagnostics](03-hir.md#diagnostics) |
| `Backend/CodeGen/*` | codegen doc 02 / 03 | Design space / the design | [04-codegen-and-abi.md](04-codegen-and-abi.md) |
| `Backend/CodeGen/*` | codegen doc 04 | ABI, interop and GC | [04-codegen-and-abi.md, ABI](04-codegen-and-abi.md#windows-x64-abi), [GC emission](04-codegen-and-abi.md#gc-emission) |
| `Backend/CodeGen/*` | codegen doc 05 | Feature matrix: HIR node -> LLVM | [04-codegen-and-abi.md, Node emission](04-codegen-and-abi.md#node-emission) |
| `Backend/CodeGen/*` | codegen doc 06 | Build plan, linking, EH | [04-codegen-and-abi.md](04-codegen-and-abi.md#exceptions), [01-pipeline.md](01-pipeline.md) |
| anywhere | "plan 14" | Reflection redesign | [../runtime/02-reflection.md](../runtime/02-reflection.md) |
| anywhere | "plan 07 / doc 07 Bug D" | Old known-bugs list | [../status/implementation-status.md](../status/implementation-status.md) |
| anywhere | "spec N.N", `docs/lang-design/01-...` | Old language specification | [../language/](../language/) |

When you touch a file with such a comment, prefer replacing it with a link to the new page.
