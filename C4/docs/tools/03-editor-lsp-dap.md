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
| Lifecycle | `initialize`, `initialized`, `shutdown`, `exit`, `workspace/didChangeConfiguration` |
| Sync | `didOpen`, `didChange`, `didClose`, `publishDiagnostics` |
| Navigation | `definition`, `declaration`, `typeDefinition`, `implementation`, `references`, `documentHighlight` |
| Information | `hover`, `signatureHelp`, `completion` (triggered by `.` and the `>` of `->`; after an accessor only the base's members) |
| Symbols | `documentSymbol`, `workspace/symbol` |
| Presentation | `semanticTokens/full` (20 token types: the 19 standard ones plus a C4 `controlFlow` kind; modifiers include `builtin`, used for primitives; a token never crosses a line - a block comment is one token per line), `foldingRange` |
| Edits | `rename`, `prepareRename`, `formatting`, `rangeFormatting` |
| Code actions | quick fixes, create-definition, tidy |

### Configuration and deployment

- The server takes no command-line arguments. It reads `rootUri` and `initializationOptions`; the client
  passes the contents of the build-derived LSP options file (`includePaths`, `defines`, `c4Home`: the C4
  install the project builds with, `projects`: the build description C4.exe gets, and `target`: the project's
  `-march` / `-m` compiler arguments, so the editor sees the build's `SIMD_*` macros and the intrinsics its
  processor allows, D-62; flags that select no target are ignored), which Nuke writes on every build
  (`.ied/local/lsp-options/<project>.json`). With a usable description
  the server compiles its projects, each with its own defines and boundary
  ([../language/15-projects.md](../language/15-projects.md)); otherwise the workspace folder and `includePaths`.
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
  and the server end to end over its message API).

### Open items

| Item | Status |
|---|---|
| Incremental re-analysis (today each change recompiles the program). | **Planned** |

---

## 2. The C4 plugin for IED

`IedPlugin/` is a declarative IED plugin (no code: a manifest, a grammar and a settings schema). `MakeToolchain.py`
copies it into every toolchain as `<toolchain>/ied/`, so a workspace gets the plugin of the C4 it builds with: the
solution `nuke init --ied` writes names it in its `localPlugins` (IED loads local plugins in trusted workspaces).

- **The language** `c4` for `.cf` and `.ch`: comments, brackets, closing pairs, indentation, and a Monarch grammar
  (`grammars/c4.monarch.json`) that colors a file until C4LSP's semantic tokens arrive.
- **C4LSP** (`lspProfiles`, IED `docs/24`): one server per project of the solution (`scope: project`), each with
  the options Nuke writes for that project (`.ied/local/lsp-options/<project>.json`) as its initialization options,
  so it compiles what the build compiles. It runs from a staged copy of `tools/C4LSP/` (rebuilding the toolchain
  never fights a running server), with `C4_HOME` naming the toolchain so the copy still finds Bedrock. Its quirks
  are declared rather than worked around in IED: byte columns (`positionEncoding: bytes`), integer request ids,
  semantic tokens and folding fetched again after each diagnostics publish (it sends no refresh requests yet), and
  code actions cut to the requested range by the client (it returns them all).
- **Settings**: `c4.languageServer` (on) turns C4LSP off for a workspace.
- **Still to come**: C4DAP as a debug provider (with IED's debugger), the problem matchers and the Nuke task preset
  (with IED's tasks), and the semantic token colors as plugin data (IED's theme maps the standard types today).

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
  (every instance of a template, every piece of a `for` header); a line without code binds to the next line
  that has some, and the response reports that line. Breakpoints set before the program starts bind when
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
    handler's body (a funclet's prologue has no line of its own).
- **Stack**: `StackWalkEx` over the thread's context; each frame keeps its own registers, which locate its
  variables. A caller's line is looked up at its return address minus one.
- **Values** come from a symbol's storage (relative to a register, the frame or in a register), read with
  `ReadProcessMemory`, and are shown by their types: integers in decimal, floats, `bool`, characters,
  enums by name, pointers as addresses (a `char8*` with its text) with what they point at as children,
  references as their referent, objects and unions with their bases and fields, arrays and swizzle lanes
  with their elements. Type names use C4 spellings (`int32`, `uint8`, `float`).
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

Events: `initialized`, `stopped` (`entry`, `breakpoint`, `step`, `pause`, `exception`), `output`
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

- C4Testing `Dap.*` (48 tests): the honest pieces (`Formatting`: scalars, strings, thrown type names, UTF-8
  output boundaries, command lines; `Expression`; `Step`: every stepping decision), the server's messages with no
  program (`Server`: capabilities and `initialized`, sequence numbers, errors for an unknown request, a missing
  program, a missing source, inspecting nothing), and sessions of programs built with `C4.exe -g` (`Session`:
  the stack and locals at a breakpoint, a caller frame's locals, every kind of step, a breakpoint inside a
  step over, recursion, a step over a throw into a catch (and the variables there), a line without code, every instance of a template, a lambda, stop on entry, output and
  exit code, pause and terminate; `Inspection`: every kind of value, children, `evaluate` and its errors;
  `Exception`: uncaught, thrown, no filters).
- ToolsTest `DapAdapter`: `StdioClient.py` drives the built `C4DAP.exe` over real stdio through a session.
