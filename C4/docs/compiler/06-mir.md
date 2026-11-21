# 06 - MIR: the control-flow graph between HIR and LLVM

MIR (`C4Lib/src/Backend/MIR/`, R5) is each lowered function's control-flow graph. HIR ([03-hir.md](03-hir.md)) is a
typed tree: it says what a body does, in the shape the source has. MIR says where control goes: basic blocks,
explicit exits that run the cleanups they leave, an explicit way out for an exception, drop flags. The flow
questions are answered once, on it - whether control runs off the end (D-55), whether a case runs into the next
(D-39), which copies are last uses (D-05), which slots start zeroed - and codegen emits it instead of rebuilding
the control flow from the tree.

## Where it sits

- **Built where a body is lowered.** `LowerFunction` (`Backend/HIR/LoweringDriver`) builds the MIR of a complete
  tree (`MirBuilder`), runs the checks on it and the last-use moves, and keeps it on the tree (`HIRFunction::mir`).
  That is on `ParallelLowering`'s threads, in `ProgramLowering`, for C4.exe, a script's compile and the language
  server alike, so the checks' diagnostics are the build's in the editor too (C4Testing `Lsp.LoweringParity`). A
  nested function (a lambda's call method, a local function) is lowered by `LowerFunction` and gets its own.
- A tree lowering makes without `LowerFunction` - an invoke or type thunk, a round of global starts, a script's
  first-use start, a census counter - gets its MIR where `ProgramReach` keeps it (`EnsureMir`); none of them has a
  check or a move to make. A round's start that the storage cannot hold as data goes back into the round at
  emission (`StartupEmitter::StartAsData`, the storage's type decides), which builds the round's MIR again.
- **Read by codegen.** `FunctionEmitter` emits a tree's MIR (`MirEmitter`); the expressions in it are still HIR,
  emitted by `ExprEmitter` and its bricks as before. `BodyNeeds` reads the HIR (`HIRWalk`), whose moved copies the
  moves already replaced.

## Shape (`MirBody.h`)

| Part | What it is |
|---|---|
| Block | Statements, then one terminator. `unwind`: the step an exception raised in it goes to (none: the caller); `funclet`: the catch handler or exception copy of a cleanup its code runs in; `cleanupOf`: the cleanup whose copy it is; `mayUnwind`: it runs code (or begins a try). A block's code runs in one set of scopes: when they change, what follows goes on in a block of its own (`MirBuilder::MatchScopes`). |
| Statement | `Eval` (an HIR expression run for its effects: a call, a store), `Declare` (a `LocalDeclNode`: the slot starts zeroed or holding its initializer), `Asm`, `AtExit`, `SetFlag` (a drop flag), `StageReturn` (a return's value, computed before the cleanups it crosses run), `OnceEnd`. Each carries `at`, the line its instructions carry (as the region emitter's sticky location had it). |
| Terminator | `Goto`, `Branch` (an HIR condition), `Switch`, `Return` (nothing left to clean up), `ReturnStaged` (the one way out of the returns that crossed cleanups), `FallOff` (the end of the body), `Throw`, `Unreachable`, `LeaveHandler` (a handler's catch ends), `Unwind` (the end of an exception's copy of a cleanup: on to the next step), `OnceBegin`, `RegionEnd`, `Terminate` (a noexcept function's handler). |
| Region | A block an expression runs in place: a Sequence's block step (a copy, a braced fill, a full expression's temporaries), `new T[n]`'s start. Its MIR blocks are built where the statement is, entered from the statement and ended by `RegionEnd`; the emitter emits them where the expression names the block, as often as it does. |
| Unwind step | One step of an exception's way out: `Cleanup` (the exception's copy of a cleanup, its own blocks ending in `Unwind`), `Abandon` (a once region gives up its guard), `Dispatch` (a try's handlers take what they catch; what none takes goes on), `EndCatch` (an exception leaving a handler ends its catch). Each names the step below it (`next`). |
| Handler | A catch clause's entry block, its dispatch and its funclet; a noexcept function's catch-all has no clause and ends in `Terminate`. |
| Funclet | Code that runs apart from the function's own flow: a handler, or an exception's copy of a cleanup. Windows makes each a pad (catchpad, cleanuppad); Linux ignores them. |

`MirText` prints a body (`bb3 (unwind s1, funclet f0):` and a line a statement); C4Testing's MIR tests read it.

## Building (`MirBuilder`)

A port of what the region emitter did, making MIR blocks instead of LLVM ones:

- **Scopes.** A stack of the scopes between the entry and the point being built: a `Cleanup` for every
  `DeferNode` (a `defer`, a value local's delete, a `finally`, a temporary's end), an `Abandon` around a once
  region, a `Try` around a try body, a `Handler` around a catch clause. A block's unwind step is its innermost
  scope's (`StepOf`, made on first need); each step's `next` is the scope below it, down to the unwind floor.
- **Exits.** Running off a block pops its scopes and runs a copy of each cleanup in place. Every jump - `break`,
  `continue`, `fallthrough`, `goto` - knows the depth of its target (loops and switches define theirs where they
  begin; a backward goto has its label's, a forward one the depth of the statement it is in in the label's block)
  and runs the exit of every scope above it: a cleanup's copy, a handler's `LeaveHandler`. The scopes are popped
  while their exits are built, so a cleanup's copy sees only the scopes outside it.
- **Returns.** A return with scopes around it stages its value and goes to the one way out (`ReturnPathFrom`): a
  chain of blocks, one per scope that does something on the way (a cleanup's copy, a handler's end), each built
  once for every return that crosses it, ending in the block that returns what was staged.
- **Exceptions.** The first block that runs code in a cleanup's scope makes its step, whose copy is built with only
  the scopes outside it and a floor of its own (below the floor nothing is a target: what the copy itself raises
  leaves the function), in a funclet of its own. A try's dispatch is made with it, its `next` what the try itself
  unwinds to; its body starts in a block that may raise, so its handlers count as reached whenever the try is (C++'s
  rule for a missing return). A noexcept body runs inside a catch-all dispatch whose handler terminates.
- **Regions.** The blocks a statement's expression runs in place (`RegionFinder`: the operands `HIRWalk` follows)
  are built as regions before the statement is added; the statement names them.
- **Loops.** A loop whose condition is a constant true (`while (1)`, `for (;;)`) goes to its body unconditionally:
  it is left only by a jump. `if (true)` keeps both ways (C++'s rule).
- **Drop guards and starts.** For a variable a copy may move (`MirMoveSites::BuildOptions`), every copy of its
  cleanup is entered through a guard block, and where a variable without a declaration begins (a parameter, a catch
  variable) is noted, for `MirMoves` to elaborate.

## Checks and passes

| Pass | File | What it decides |
|---|---|---|
| Missing return (D-55) | `MirReach::FallsOffEnd` | Whether the body's end (`FallOff`) can be reached from the entry - by terminators, into regions, and along each block's unwind steps to handlers. A label counts as reached (a goto may name it). `control reaches the end of non-void function 'F': return a 'int32' on every path`, at the closing brace (`LoweringDriver`). |
| Case fallthrough (D-39) | `MirReach::CheckCases` | A case that may not run into the next (`SwitchCase::fallCheck`: not the last, not ending in `fallthrough;`) whose end can be reached from its own start: `this case falls into the next one: end it with 'break;' or 'fallthrough;'` at the case. |
| Last-use moves (D-05) | `MirMoves` | A copy of a variable (`TransferNode`) moves when no path from it reaches another use of the variable before it is declared again (a backward liveness over the blocks, cut into segments where an expression runs a region; a `?:`'s other arm does not follow), its address never escaped, and no other cleanup uses it. A moved variable gets a drop flag: `SetFlag` where it is declared (or where its block begins), cleared by the move (the transfer's `copy` becomes the flag's clear and the load), and each drop guard of its cleanup branches on it. |
| Slots zeroed at entry | `MirBody::declared` | A local slot no `Declare` starts (a temporary the lifecycle reads, a drop flag) is zeroed with its vptrs at entry (`IRContext::ZeroUndeclaredSlots`). |

The goto rules (D-15: a forward jump may not cross a declaration or a `defer` of the label's block, an undefined
label) stay a check of the source's scopes while lowering (`HIR/JumpScopes`): HIR moves a statement's nodes into a
block that owns its temporaries, so after lowering a declaration's block is no longer the block whose scope it
belongs to. The MIR builder relies on what they guarantee when it resolves a forward goto.

## Compile-time evaluation (R6, D-91)

A function marked `[compile_time]` (on any of its declarations) can run while the program compiles: its MIR is
interpreted over folded arguments.

- **The check where it is defined** (`CompileTimeCheck`, from `LowerFunction` after the flow checks): a marked
  function may do only what the interpreter runs - no `new` or `delete`, no inline assembly, no static local or exit
  registration, no global variable that is not a constant, no atomic, SIMD, variadic or run-time type operation, no
  function address, no call through a pointer, a func value or a vtable, no call of a function that is neither marked
  nor compiler-written (a create, copy or delete). Only what runs while nothing is thrown counts (the blocks reached
  from the entry without unwinding): a throw ends an evaluation with an error, so a handler never runs there. The
  first breach is the function's error: `'F' is [compile_time], but it allocates on the heap ('new')`; the mark takes
  no arguments (`[compile_time] takes no arguments`). An unmarked function is not checked and can never run at
  compile time.
- **The interpreter** (`MirInterpreter`): memory is a list of byte allocations - each slot of each frame one, laid out
  as the target lays out its type (little-endian x64) - and a pointer is an allocation and an offset in it (the
  allocation in the high 32 bits; allocation 0 is null), so reading or writing past an object is an error, not a
  wrong value. Scalars compute as the emitted code does: integers wrap in their width, `float` rounds as `float`,
  division by zero and a shift by the width or more are errors. It runs every terminator but `Throw` (an error:
  `compile-time evaluation threw an exception`) and the unwinding ones (nothing unwinds); a region runs where its
  expression names it. Two limits keep it finite: 1,000,000 statements and terminators in all (`compile-time
  evaluation ran more than 1000000 steps: a loop that does not end, or work too big to do while compiling`) and 256
  calls deep (`... went more than 256 calls deep: a recursion that does not end?`).
- **The evaluator** (`CompileTimeEvaluator`, one per program model): `Call` gives the result as a constant (a
  number, a bool, a character; a pointer or an aggregate is an error), `CallForData` the bytes of whatever it returns
  (an array or an object: a table). Each callee is lowered once by `LowerFunction` - the same HIR and MIR a build
  emits - and kept; a callee that is not marked (`'F' is not [compile_time]`) or does not compile (`'F' cannot run at
  compile time: it does not compile`; its own errors are reported where the build lowers it) is refused. Each call
  has an interpreter of its own and no lock is held while a body lowers, so constants fold on the lowering threads at
  once; a constant in a function's own body that needs that function is refused (`'F' cannot run at compile time
  while it is compiled`). Only bodies that compiled are kept: one that failed while the model resolved is lowered
  again when asked later. While the model is still resolving (a field's extent or an enum value folds in pass 2),
  the types a callee takes and returns are completed before its body lowers, and every type its slots hold before it
  runs (`DeclarationQueries::Complete`), so it never reads a half-built layout; the callee itself is resolved first
  (`DeclarationQueries::Functions`: free functions resolve last in pass 2), and a cycle through its signature is said
  as R9 says one (`the alias 'Row' depends on itself: Row -> Count -> Row`).
- **Where a constant calls it.** `ConstEvaluator::FoldCall` (`Model/ConstCalls.cpp`): a call written with a
  function's name (`Square(3)`, `Tables::Square(3)`) in any constant position. When no overload of the name is
  marked (`Model/CompileTimeMark`) it fails at once - `'Seed' is not [compile_time]: only a function marked
  [compile_time] runs in a constant` - so a call that never ran at compile time resolves no overload and
  instantiates nothing; else its arguments fold, the overload is chosen as a call chooses it (their written
  types, or a folded value's), and the model's `ICompileTimeCalls` (`Model/CompileTimeCalls.h`) runs it. The
  model owns no evaluator: `CompilerInstance` makes one with each model it builds (C4.exe, C4LSP and scripts
  alike) and sets it on the model; a model without one folds no calls.
- **A global's table** (`HIR/CompileTimeImage`, from `GlobalInit`'s starts after `ConstantImage`): a global
  whose initializer is such a call and whose type is plain data (no pointer - an evaluation's pointers are not
  the program's -, no vtable, nothing to destroy) starts holding the bytes `ConstEvaluator::EvaluateData` gets
  from `CallForData`, as a constant start; anything else, or a call that cannot run, starts as code before
  `main` as before.

## Emission (`Backend/CodeGen/MirEmitter`)

- Every block control reaches from the entry is emitted, from the function's entry block on, the others as a
  terminator first names them (a work list): no code after a `return` or `throw` is emitted to be deleted later.
  A block that is its predecessor's `Goto`'s only way in continues in that block's LLVM block, so MIR's extra blocks
  cost no branch.
- A region is emitted where its expression names its block (`EmitNestedBlock`), its blocks with LLVM blocks of
  their own each time; the expression goes on where `RegionEnd` was.
- `IRContext::Call`, the one place a call is made, asks the block being emitted where it unwinds and which
  funclet it runs in (`BlockServices`).
- **One emitter per exception scheme** (`EhLowering`, picked by `TargetAbi::Exceptions`):
  - `FuncletLowering` (Windows): a `Cleanup` step is a cleanuppad that begins its copy's first block and is left by
    cleanupret to the step below; a `Dispatch` a catchswitch whose handlers' entries begin with their catchpads
    (`[TypeDescriptor, adjectives, slot]`, `[null, 64, null]` for a catch-all), left by catchret; an `Abandon` a
    cleanuppad calling the runtime; an `EndCatch` is no step (an exception raised in a handler unwinds where its
    try's catchswitch does). Calls inside a funclet carry its pad.
  - `LandingPadLowering` (Linux, D-86): an exception raised with step `s` lands at `s`'s landingpad, whose clauses are
    those of every dispatch on its way out up to a catch-all, then takes each step in turn - a cleanup's copy, an
    abandon, `c4_eh_end_catch`, a dispatch comparing the selector with `llvm.eh.typeid.for` - and below the last
    resumes. A handler begins with `c4_eh_begin_catch`; `LeaveHandler` ends the catch.
  Either makes a step's blocks the first time a call needs them, so a scope nothing in which can raise has no pad.

## Files

| File | Role |
|---|---|
| `MIR/MirBody.{h,cpp}` | The data: blocks, statements, terminators, regions, steps, handlers, funclets; `EnsureMir` |
| `MIR/MirBuilder.{h,cpp}`, `MirBuilderScopes.cpp` | HIR to MIR: blocks, statements, control flow and labels; scopes, exits, the way out of returns, unwind steps, try and catch, once regions |
| `MIR/MirReach.{h,cpp}` | Reachability: the missing-return and case-fallthrough checks |
| `MIR/MirMoves.{h,cpp}` | D-05's last-use moves and their drop flags |
| `MIR/MirText.{h,cpp}` | A body as text |
| `MIR/MirInterpreter.{h,cpp}`, `MirInterpreterValues.cpp` | Compile-time evaluation: control flow; values, places and memory |
| `MIR/CompileTimeCheck.{h,cpp}` | The `[compile_time]` mark and the check where a marked function is defined |
| `MIR/CompileTimeEvaluator.{h,cpp}` | The evaluator a constant calls: kept bodies, a call's arguments and result |
| `Model/CompileTimeCalls.h`, `CompileTimeMark.{h,cpp}`, `ConstCalls.cpp` | The model's side: the evaluator's interface, the mark, `ConstEvaluator::FoldCall` and `EvaluateData` |
| `HIR/CompileTimeImage.{h,cpp}` | A global that starts as the data a `[compile_time]` call returns |
| `CodeGen/MirEmitter.{h,cpp}` | MIR to LLVM |
| `CodeGen/EhLowering.h`, `FuncletLowering.{h,cpp}`, `LandingPadLowering.{h,cpp}` | The exception schemes |

## Tests

C4Testing `Mir.Reach` (reachability on hand-made HIR: jumps, branches, loops, switches, tries, labels, case
checks), `Mir.Builder` (the graph of lowered bodies: exits, the shared way out of returns, unwind steps, dispatch and
handlers, regions, noexcept, once regions, drop flags, no MIR for an incomplete body), `Mir.Emitter` (funclets for
Windows and landing pads for Linux from one MIR, a noexcept body on both, straight-line code as one block),
`Semantic.LastUseMoves` (the moves on the flow: goto, exclusive branches, an exception path), `Mir.Interpreter`
(scalars, wrapping, value types and methods, loops and switches, recursion, arrays and tables, the two limits, what
compile time cannot do), `Mir.CompileTime` (the mark, the definition check's errors, the evaluator's results and
refusals, data as the target lays it out, every constant position through the model, a global table as data),
`Lsp.LoweringParity`;
TestCode `Functions/CompileTime` (each constant position end to end, at `-O0` and `-O2`); NegativeTests
`CompileTime*` and `ConstantCallsUnmarked` (each diagnostic);
NegativeTests `SwitchImplicitFallthrough`, `SwitchFallthroughOnOneBranch`, the `control reaches the end` family; every
end-to-end suite (IsolatedTest, RuntimeTest, the Bedrock tests) runs MIR-emitted code on Windows, and the Linux suites
on landing pads.
