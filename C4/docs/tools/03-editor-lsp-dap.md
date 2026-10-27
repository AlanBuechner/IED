# Editor support: C4LSP, C4DAP

C4's editor integration targets **IED**, the project's own IDE. IED has no built-in C4 knowledge: all
coloring, navigation and diagnostics come from the C4 language server, and debugging from the C4 debug
adapter, through the C4 plugin for IED (`IedPlugin/`; it replaces the plugin written for the previous editor,
which was retired). The server and the adapter speak plain LSP and DAP, so any client can use them.

| Piece | Location | Status |
|---|---|---|
| C4LSP - language server | `C4LSPLib/` (the server), `C4LSP/` (the executable) | **Implemented** (rebuilt after the ProgramModel rework, validated in the previous editor, unit tested in C4Testing) |
| C4 plugin for IED | `IedPlugin/`, shipped in every toolchain as `ied/` | **Implemented** for the language and C4LSP; the C4DAP part waits for IED's debugger |
| C4DAP - debug adapter | `C4DAPLib/` (the adapter), `C4DAP/` (the executable) | **Implemented** (unit tested in C4Testing and over stdio in ToolsTest) |

---

## 1. C4LSP

### Architecture rule: the compiler is the single source of truth

The server never re-implements language rules. It links C4Lib, compiles the workspace with the same
pipeline as C4.exe, and answers every request from the compiler's own data:

- **Diagnostics**: parse errors per file plus ProgramModel errors, plus the errors from running the same
  HIR lowering pass C4.exe runs over every function body. Lowering is skipped when the front end already
  has hard errors (exactly like C4.exe). Results are de-duplicated by (line, column, message), so the
  editor shows exactly what a build would print.
- **Semantic queries** go through `Server/SemanticResolver`, which drives the ProgramModel's own query
  verbs (qualified lookup, type resolution in a unit's context, variable and member queries) using
  scope context derived from the AST path.
- **Interop** blocks are processed declarations-only.
- **The default library** is registered like C4.exe does it: the Bedrock files the program includes (and their
  implementations), from the install the client names in `c4Home`, else `Toolchain::Bedrock()` (the install the
  server runs from, or the repository's `Bedrock/` in a development build).

If a feature needs a fact the model does not expose, the fix is a new model query, not logic in the LSP.

### Layout

```text
C4Protocol/src/Json/          JSON values, Content-Length framing, the stdio channel (shared with C4DAP)
C4LSPLib/src/                 static library: everything the server does (C4Testing links it too)
  Server/     LspServer, StdioTransport, CompilationSession, NodeLocator, SemanticResolver
  Protocol/   LspJson, LspTypes, TextDocumentStore, TextDocumentSync
  Features/   one file per feature (below)
C4LSP/src/main.cpp            the executable: the stdio loop
```

### Supported requests

| Area | Methods |
|---|---|
| Lifecycle | `initialize` (answers `positionEncoding`: `utf-8` when the client offers it, else `utf-16`; request ids, numbers or strings, come back as sent), `initialized`, `shutdown`, `exit`, `workspace/didChangeConfiguration` |
| Sync | `didOpen`, `didChange`, `didClose`, `publishDiagnostics` |
| Navigation | `definition` (a function's body: its out-of-line definition when it has one), `declaration`, `typeDefinition` (the named type of a value: a variable's or a parameter's at its declaration or a use, through references, pointers and aliases), `implementation` (from a type, the types deriving from it; from a method, the methods of derived types that override it), `references` and `documentHighlight` (asked at a use, at a declaration's own name or at a type written in a declaration - `Push` of `void Ring::Push()` is the method, its `Ring` the type, `Ring` of `Ring* ring` the type; every file, the declarations first with `context.includeDeclaration`; a type's references include where it is written as a type, `Ring&` of a parameter, a field's type in another header, `new Ring(...)`, the `Ring::` of each out-of-line definition, and each base list naming it, `class Square : IShape`; a create operator's references are the constructions that call it, `new Ring(args)`, `Ring r(args)`, `Ring r;` and `Ring(args)`, by its count of arguments; a member reached with `->` through a pointer resolves like one reached with `.`; a name outside any function, a field's type in `namespace N { class C { ... } }`, resolves from its type's and namespace's scope); `rename` renames a type's qualifiers too but leaves a constructor's constructions, which spell the type's name; `prepareCallHierarchy`, `callHierarchy/incomingCalls` and `outgoingCalls` (the function named or called at a position; its callers, each with its calls; the functions its body calls; a function's declaration in its type and its out-of-line definition are one function; a construction is a call of the create operator it fits); `prepareTypeHierarchy`, `typeHierarchy/supertypes` and `subtypes` (direct bases and direct derived types) |
| Information | `hover` (markdown `MarkupContent`: a `c4` block with the declaration as written - annotations, access, modifiers, return type, parameters with their defaults, trailing qualifiers - its name qualified with its owner, `public int64 Ai::ToolRegistry::Find(const Span& text, int64 start = 0) const`, then the comment above it, or after it on a field's line; a method's out-of-line definition hovers as its declaration in the type; a parameter or a local is labeled as one; a type written in a declaration shows that type; on a macro use, the `#define` and what the use expands to), `signatureHelp`, `completion` (triggered by `.` and the `>` of `->`; after an accessor only the base's members), `c4/expandMacro` (`TextDocumentPositionParams` -> `{name, expansion}`, null off a macro use; `experimental.expandMacro` in the capabilities, as rust-analyzer's `rust-analyzer/expandMacro`) |
| Symbols | `documentSymbol` (kinds Namespace, Class, Struct, Interface, Enum, EnumMember, Method, Constructor - an `[operator("create")]` method - Field, Function, Variable, Constant - a `const` global; `detail` is the declaration without its annotations, access and name, `static int64 (const Span& text) const`; an out-of-line definition is named with its owner, `ToolRegistry::Add`, as clangd names one; a range covers the doc comment above and the annotations; each `#define` is a symbol of kind Function with detail `macro`), `workspace/symbol` (names containing the query, any case, inside namespaces and types too; `containerName` is the `::`-joined owner, `Ai::ToolRegistry`, and an out-of-line method's is its type; every `#define`, kind Function, where it is defined) |
| Presentation | `semanticTokens/full` (21 token types: the 19 standard ones plus the C4 `controlFlow` kind and `preprocessor` for directive keywords - `#define`, `#if`, ... and `include`; modifiers include `builtin`, used for primitives, also behind a pointer or an array (`char` of `const char*`); a type named in an expression (a constructor call) keeps its own kind, and a method called through an implicit `this` is a `method`; a token never crosses a line - a block comment is one token per line), `foldingRange` |
| Edits | `rename` (a function's declaration, its out-of-line definition and every use), `prepareRename`, `formatting`, `rangeFormatting` |
| Code actions | quick fixes, create-definition, tidy |

### Configuration and deployment

- The server takes no command-line arguments. It reads `rootUri` and `initializationOptions`. A program's options
  are what the build-derived LSP options file holds (`includePaths`, `defines`, `c4Home`: the C4 install the project
  builds with, `projects`: the build description C4.exe gets, and `target`: the project's `-march` / `-m` compiler
  arguments, so the editor sees the build's `SIMD_*` macros and the intrinsics its processor allows, D-62; flags that
  select no target are ignored), which Nuke writes for every program it compiles, also when the compile fails
  (`.ied/local/lsp-options/<program>.json`; a module's tests are a program). `programs` (`{<name>: <options>}`) names
  every program of the workspace, and the options' own fields are one more program (one server for one program, as
  before). With a usable description a program compiles its projects, each with its own defines and boundary
  ([../language/15-projects.md](../language/15-projects.md)); without one, the files beside each open document it
  takes (not the folders under them: a workspace folder can hold many programs) and everything under `includePaths`.
  `workspace/didChangeConfiguration` with `{settings: {programs}}` replaces the programs (an options file changed); a
  program whose options are the same keeps its analysis, and one whose build description file changed is analyzed
  again.
- `scripts` (`[{folders: [...], interface: "<host>.c4i", allow: [...], defines: ["NAME=VALUE"]}]`) names folders of
  scripts: a document under one of them is analyzed as C4Script compiles it ([../runtime/04-scripting.md](../runtime/04-scripting.md)),
  against that host's interface (read once, again when the file changes), with every `.cf` and `.ch` under the
  folders, the allow-list and the defines, and is not part of the program. Its diagnostics are the script compile's
  front end and body checks, the rules for scripts included (no `extern` import, no header off the allow-list, no
  body for a host function); an interface that does not read is an error on each of its documents. A host header a
  script includes lives in the interface, so navigation stops at the script's own files.
- File and folder names may use any script: the compiler keeps paths as UTF-8.
- An editor document maps to its source by absolute path (case-insensitive on Windows), and a source's URI is its
  file, so navigation works in every project, `-I` folder and Bedrock alike.
- Every toolchain ships it as `tools/C4LSP/C4LSP.exe`, with the LLVM dlls it loads
  ([02-nuke.md](02-nuke.md#5-toolchains)).
- Options a client can change live (`initializationOptions` at startup, then
  `workspace/didChangeConfiguration` with `{settings: {...}}`): `tidy` (C4 Tidy diagnostics, off in a bare
  session), `quickfixes` and `createDefinition` (the code-action families, on by default). Unknown keys are
  ignored.
- Built as part of the normal compiler build (`C4Protocol`, `C4LSPLib` and `C4LSP` are in the root `Build.py`
  module list).
- Tests: `C4Testing/src/Tests/Protocol/` (JSON values and the framing: partial frames, several frames in one
  chunk, a frame with no usable length or a body that is not JSON is dropped and reading goes on) and
  `C4Testing/src/Tests/LSP/` (protocol types, document sync, the session and the default library, every feature,
  and the server end to end over its message API; `Lsp.Programs`: one server for an engine, a game module on it, a
  client and a tool - a module analyzed in a program that compiles it, the analyses the open documents need, queries
  across programs, programs arriving with a configuration change, and the planner).

### One server for every program

A workspace holds many programs - Claymore's editor, client, server and tools, each with the modules it depends on - and
a module's files are compiled only inside the programs that use it. One server serves them all (`Server/Program`,
`ProgramPlan`, `LspServer`):

- **Each open document is analyzed in one program that compiles it** - a described project's sources hold it. As few
  programs as hold all the open documents are analyzed, chosen one at a time: the one holding the most documents left,
  one already analyzed before one that is not, then the one with fewer projects. Boundaries make a project mean the
  same in every program that holds it, so a module's file reads the same in any of them (and gets what a build of that
  program prints). A document no program holds goes to the options' own program (the loose files beside it). Script
  documents are their scope's, as before.
- **An analysis is redone when what it took changed**: the open documents it holds (their text), its build
  description file, its options. An edit redoes the programs holding the edited document; the others keep theirs. A
  program no open document needs any more lets its analysis go.
- **A query about a declaration asks every program that reaches it**: `references`, `implementation` (every override
  of a virtual method, in every project that includes the declaring one), `rename`, `callHierarchy/incomingCalls`,
  `typeHierarchy/subtypes` and `workspace/symbol`. Besides the document's own program, as few programs as hold every
  project that uses the declaring one (every project, for a file of the default library) are asked - at the same place
  when they compile the document, else at the declaration - analyzed for the query when they were not (a request waits
  25 s at most in all, under IED's 30 s), and kept three minutes after; answers are merged, each place once. A
  parameter or a function's local asks no other program.
- **Threads.** Analyses are made on the worker; a request reads them on the reading thread while the worker publishes
  diagnostics from them, so the two take turns (the resolver's caches and the model's query-time instantiations are not
  thread-safe): a request holds them but while it waits for the worker. A setting that changes what the diagnostics say
  (`tidy`) republishes them on the worker, after any older publish. `workspace/symbol` asks programs that
  hold every project, so a search of the workspace's symbols costs every program's analysis for those three
  minutes.

### Memory

Measured on Claymore and IED's own workspace (IED `docs/24` "C4LSP memory, measured"): one server per project took 1.6
and 4.9 GB, growing with every edit; one server for the workspace 0.26 and 1.1 GB, steady.

- **One parse at a time.** Parsing a program's files on every processor at once left the heap holding several times
  what the program needs, and more after every compile: a program including `bedrock.ch` was 85 MB of objects in a
  400 MB process growing 20 MB an edit. The server's compiler parses one file at a time
  (`CompilerInstance::SetMaxThreads`): that program now takes 136 MB steady, IED's server program 0.6 GB (first
  analysis 3.3 s instead of 2.0 s, an edit 1.5 s instead of 1.1 s). C4.exe still parses on every processor: it compiles
  once and exits.
- **Tokens go after the parse.** A file's tokens (as many bytes as its AST: the AST keeps copies of the ones it needs)
  are released when its parse ends (`Lexer::ReleaseTokens`), in C4.exe too.
- An analysis is double-buffered while it is redone: requests answer from the last one meanwhile.

### Open items

| Item | Status |
|---|---|
| Incremental re-analysis (today each change recompiles the program). | **Planned** |
| One parse of a module several analyzed programs share (each program parses Bedrock and its modules today; plan B76). | **Planned** |

---

## 2. The C4 plugin for IED

`IedPlugin/` is a declarative IED plugin (no code: a manifest, a grammar and a settings schema). `MakeToolchain.py`
copies it into every toolchain as `<toolchain>/ied/`, so a workspace gets the plugin of the C4 it builds with: the
solution `nuke init --ied` writes names it in its `localPlugins` (IED loads local plugins in trusted workspaces).

- **The language** `c4` for `.cf` and `.ch`: comments, brackets, closing pairs, indentation, task comments, and a
  Monarch grammar (`grammars/c4.monarch.json`) that colors a file until C4LSP's semantic tokens arrive. Its files and
  projects show a teal "C4" (a header an "h"), and `.ch` / `.cf` pair as header and source.
- **Semantic token colors** (`semanticTokenColors`): C4LSP's token types mapped onto IED's syntax theme tokens in the
  Visual Assist style (`controlFlow` pink, `preprocessor` gray), never raw colors.
- **C4LSP** (`lspProfiles`, IED `docs/24`): one server for the workspace (`scope: workspace`), given every
  program's options Nuke writes (`initializationOptionsFiles` `.ied/local/lsp-options/*.json` under `programs`, sent
  again when one changes), so each document is analyzed as the build compiles it (section 1 "One server for every
  program"). It runs from a staged copy of `tools/C4LSP/` (rebuilding the toolchain
  never fights a running server), with `C4_HOME` naming the toolchain so the copy still finds Bedrock. It agrees
  the position encoding with the client (`utf-8` when offered, else `utf-16`) and answers string or number request
  ids as sent. Two quirks are declared rather than worked around in IED: semantic tokens and folding are fetched
  again after each diagnostics publish (`refetchOnDiagnostics`: it sends no refresh requests yet), and code actions
  are cut to the requested range by the client (`codeActionsNeedClientFilter`: it returns them all).
- **C4DAP** (`dapProviders`): the debug adapter for type `c4` (section 3), staged like the server, in trusted
  workspaces.
- **Build output**: the problem matchers `c4` (`file (line:col) error: ...`) and `c4-test` (`FAIL: file:line: ...`),
  with output links for both shapes, so errors and failed tests land in IED's error list and open at their line.
- **The task preset** `c4.nuke` (`taskPresets`): IED's `nuke` preset for a folder with a `nuke.toml`, with C4's
  matchers and a `Launch` debug configuration (C4DAP on the project's output, built first).
- **AI**: the skills of this version of the language (`skills/`, read before writing C4 or its tests) and a system
  message section for workspaces that hold C4.
- **Settings**: `c4.languageServer` (on) turns C4LSP off for a workspace.

The server and the adapter are tested here without an editor (C4Testing `Lsp.*` and `Dap.*`, ToolsTest); IED's
server checks drive its language server pool with a fake server.

---

## 3. C4DAP (debugger)

C4DAP is a Debug Adapter Protocol server for C4 programs built with `-g`
([compiler/04-codegen-and-abi.md](../compiler/04-codegen-and-abi.md#debug-information--g); Nuke passes `-g` for
Debug and Development). A DAP client (IED, VS Code, any other) starts `C4DAP.exe` and talks to it over
stdin and stdout with the same `Content-Length` framing as the language server. Every toolchain ships it as
`tools/C4DAP/C4DAP.exe`.

### Layout

```text
C4Protocol/src/Json/        static library shared with C4LSPLib: JSON values, Content-Length framing,
                            the stdio channel
C4DAPLib/src/               static library: everything the adapter does (C4Testing links it too)
  Server/   DapServer (dispatch, lifecycle), DapExecution (breakpoints, run and step), DapInspection
            (threads, stack, scopes, variables, evaluate)
  Target/   the debugger: Debuggee (the engine's thread), Engine (the session; EngineStops, EngineInspection),
            Process, Launch, OutputPipes, Symbols, Patches, SourceBreakpoints, Step, Frames, Values,
            ScalarText, Expression, ThrownType, WideText
C4DAP/src/main.cpp          the executable: the stdio loop over DapServer
```

### The debugger

The adapter is its own small debugger over the Win32 debug API and DbgHelp, both part of every Windows
install. (The system's `dbgeng.dll` refuses to load its symbol engine outside the Debugging Tools for
Windows, so it cannot be the engine; and the debug API lets the adapter give the program its own
handles.)

- **One engine thread** owns the debuggee: it creates the process (`DEBUG_ONLY_THIS_PROCESS`), so it is the
  thread that may wait for its debug events, and it is the only thread that calls DbgHelp (which is
  single-threaded). The server posts commands to it and waits for their results; while the program runs,
  the engine polls for commands between short waits for debug events.
- **The program's handles**: stdout and stderr are pipes the adapter reads and forwards as `output` events;
  stdin is `NUL`. The adapter's own stdin and stdout are not inheritable, so the program can never write
  into the protocol.
- **Symbols** (`SymInitialize` on the process, a module per `CREATE_PROCESS` / `LOAD_DLL` event): `file:line`
  to addresses, an address to its function and line, the scope's parameters and locals (`SymSetContext`,
  `SymEnumSymbols`), and their types (`SymGetTypeInfo`).
- **Breakpoints** are `int3` patches keyed by address. A source breakpoint binds to every address of its line
  (every instance of a template, every piece of a `for` header, every place the optimizer inlined a call of its
  function: `ESLFLAG_INLINE_SITE`); a line without code binds to the next line that has some, and the response
  reports that line. A function's line table gives an inlined call's code the call's line all the way through, so
  a line binds to its own code (where the innermost frame is on it), and only where a function has none of that, to
  the first of the inlined code; a stop there shows the caller's frame on the line, outside the inlined calls. Breakpoints set before the program starts bind when
  the program's module loads (a `breakpoint` event says so). Resuming from a breakpoint steps the original
  instruction with the trap flag, then puts the patch back.
- **Stepping** is by source line, single-stepping instructions:
  - `next` runs a call it reaches to its return address (a temporary breakpoint that only counts when the
    stack pointer is back where the call left it, so a recursive call does not end the step early);
  - `stepIn` follows a call into code that has lines, and runs code without lines (the runtime, the C
    library) to its return;
  - `stepOut` runs to the return address of the current frame;
  - a step ends when the line or the function changes; hitting a breakpoint, an exception or the end of the
    program ends it too;
  - a throw that leaves the call a step runs over ends the step in the catch that takes it: on the throw's
    first chance the adapter puts stops on the catch handlers of the stepped frame and its callers, read from
    each function's x64 unwind information and MSVC C++ tables (`CatchHandlers`), at the first line of each
    handler's body (a funclet's prologue has no line of its own);
  - a call the optimizer inlined has no call instruction, so each instruction's inline depth stands for it
    (`SymAddrIncludeInlineTrace`, its line from the innermost inlined call's): `next` single-steps through code
    deeper than where it started, `stepIn` stops on its first line, and `stepOut` of an inlined call single-steps
    until the instruction is no longer in it.
- **Stack**: `StackWalkEx` over the thread's context; each frame keeps its own registers, which locate its
  variables. A caller's line is looked up at its return address minus one. The walk reports each call inlined at a
  frame's pc as a frame of its own (`InlineFrameContext`), innermost first; its name, line and locals come from
  `SymFromInlineContext`, `SymGetLineFromInlineContext` and `SymSetScopeFromInlineContext`, and it shares its
  caller's registers and pc (`instructionPointerReference`).
- **Values** come from a symbol's storage (relative to a register, the frame or in a register: any part of a general
  register, `rax` to `al` and `ah`, `r8` to `r8b`, or the low 64 bits of `xmm0` to `xmm15`), read with
  `ReadProcessMemory`, and are shown by their types: integers in decimal, floats, `bool`, characters,
  enums by name, pointers as addresses (a `char8*` with its text) with what they point at as children,
  references as their referent, objects and unions with their bases and fields, arrays and swizzle lanes
  with their elements. Type names use C4 spellings (`int32`, `uint8`, `float`).
- **Optimized code** (`-O1` to `-O3`, so Nuke's Development) keeps a variable in a register, on the stack or
  nowhere, and that changes along the function: DbgHelp gives the storage at the frame's address. A variable with no
  location there shows `<optimized away>` (as in Visual Studio) and `evaluate` says it is optimized away here; one
  the debug info gives as a constant shows that constant. A local in a register the adapter does not know shows
  `<unreadable>`.
- **Exceptions**: a C4 exception (MSVC C++ EH) stops the program when nothing catches it (`uncaught`, on by
  default) and when it is thrown (`thrown`); the stop names the thrown type, read from the throw's MSVC
  tables (`ThrownType`). With a debugger attached, Windows hands an uncaught exception to the debugger (a
  second chance) instead of running the program's terminate handler, so the stop shows the throw's stack, and
  after it the program ends with the exception's code (0xE06D7363) rather than the runtime's exit 1. An
  exception that leaves a `noexcept` function is caught by the compiler's guard, which reports through
  `c4_report_uncaught_exception`; the adapter stops there too. Any other exception stops the program when
  unhandled. The debugger's own breakpoint and single-step exceptions are never passed to the program.
- **Threads**: while one thread steps over a breakpoint's original instruction the others are suspended, so
  none passes the breakpoint unseen. The thread a `pause` injects is not listed; the stop is reported on the
  main thread.

### Requests

| Request | What it does |
|---|---|
| `initialize` | Capabilities: `supportsConfigurationDoneRequest`, `supportsEvaluateForHovers`, `supportsTerminateRequest`, exception filters `thrown` and `uncaught` (on by default). Then the `initialized` event. |
| `launch` | `program` (required), `args`, `cwd`, `stopOnEntry` (stop at the first line of `main`). Starts the program held at its start until `configurationDone`. |
| `setBreakpoints`, `setExceptionBreakpoints` | Replace a source's breakpoints / the exception filters. |
| `configurationDone` | Lets the program run. |
| `threads`, `stackTrace`, `scopes`, `variables` | The program's threads; a thread's frames with their function, source and line; a frame's `Locals` scope; a value's children. |
| `evaluate` | A variable of the frame or a global, then any chain of `.field`, `->field`, `[index]` and a leading `*` (hover and watch). |
| `continue`, `next`, `stepIn`, `stepOut`, `pause` | Run and step; `pause` breaks into the running program. |
| `disconnect`, `terminate` | End the program and the session. |

Events: `initialized`, `process` (the program's `name` and `systemProcessId` once it starts, so a client can
graph what it uses), `stopped` (`entry`, `breakpoint`, `step`, `pause`, `exception`), `output`
(`stdout` / `stderr`), `thread`, `breakpoint`, `exited` (the exit code), `terminated`. Frame ids and
variable references live until the program runs again.

Not supported (the capabilities say so): `attach`, `setVariable`, conditional, hit-count and log points,
data breakpoints, `restart`, `goto`, reverse execution, the `modules` and `loadedSources` views.

### Using it from an editor

A DAP client runs the adapter as a `stdio` adapter: command `<toolchain>/tools/C4DAP/C4DAP.exe` (`nuke toolchain
which` prints the workspace's toolchain folder), adapter id `c4`. A launch configuration builds with Nuke
(`nuke build`) first, then sends `launch` with the program Nuke builds:

```json
{ "program": "${workspaceFolder}/build/bin/Development-windows-x64/Game/Game.exe", "stopOnEntry": false }
```

### Validation

- C4Testing `Dap.*` (57 tests): the honest pieces (`Formatting`: scalars, strings, thrown type names, UTF-8
  output boundaries, command lines; `Expression`; `Step`: every stepping decision), the server's messages with no
  program (`Server`: capabilities and `initialized`, sequence numbers, errors for an unknown request, a missing
  program, a missing source, inspecting nothing), and sessions of programs built with `C4.exe -g` (`Session`:
  the stack and locals at a breakpoint, a caller frame's locals, every kind of step, a breakpoint inside a
  step over, recursion, a step over a throw into a catch (and the variables there), a line without code, every instance of a template, a lambda, stop on entry, output and
  exit code, pause and terminate; `Inspection`: every kind of value, children, `evaluate` and its errors;
  `Exception`: uncaught, thrown, no filters; `Optimized`: DbgHelp's flags to storage, every part of a
  register, constants and variables optimized away, an `-O3` loop's locals, an inlined call's frame, locals and
  step out, and a breakpoint on an inlined call's line).
- ToolsTest `DapAdapter`: `StdioClient.py` drives the built `C4DAP.exe` over real stdio through a session.
