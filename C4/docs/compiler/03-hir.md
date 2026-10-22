# 03 - HIR: typed lowering of function bodies

HIR (High-level Intermediate Representation, `C4Lib/src/Backend/HIR/`) is where function bodies are
understood. The program model knows every declaration but never lowers a body; HIR lowers one function at a
time, on demand, by asking the model questions, and produces a typed, de-sugared tree that codegen can emit
without any further semantic reasoning. All body-level validation happens here, which is why invalid code
yields diagnostics rather than crashes.

## The contract

1. **Input:** one `FunctionRecord` plus the `ProgramModel`. Nothing else - no AST outside the record's
   body, no shared "current function" state.
2. **Output:** an `HIRFunction` returned by value (it owns its node arena), and diagnostics in the
   `ErrorTracker`. `complete == false` means lowering failed; codegen never emits an incomplete tree.
3. **Every node's `type` is concrete.** No aliases to chase, no template parameters, no unresolved names.
   Implicit conversions are explicit `ConvertNode`s; operator calls are explicit `CallNode`s with the
   chosen record; virtual calls carry their vtable slot.
4. **HIR never mutates the AST.** Template bodies are shared by every instantiation; bindings come from
   scope layers, not rewriting.
5. **HIR never re-derives what the model owns.** Field offsets, sizes, overload choices, operator methods,
   visibility and constants all come from model queries.
6. **No optimization in HIR.** Lowering is a direct mapping; optimization is LLVM's job (`-O<n>`).

Entry point: `HIRFunction LowerFunction(ProgramModel&, ErrorTracker&, const FunctionRecord*)`
(`LoweringDriver`). It creates a transient `LoweringContext`, pushes the function scope, binds parameters
as locals (names from the record, reference params as auto-deref lvalues), and for an instantiated
template body pushes the fake-alias bindings so `T`/`N` resolve through ordinary lookup. Every layer pops
by RAII, even on error.

## Files

| File | Role |
|---|---|
| `HIR.h` | Umbrella header |
| `HIRValue.h` | `LValue` (addressable location descriptor) and `Value` (rvalue node or lvalue) |
| `HIRNodes.h` | The closed node taxonomy and `HIRFunction` (arena, slots, nested functions) |
| `ModelQueries.{h,cpp}` | The ONE model-facing surface: query wrappers bound to the asking context, constructor/destructor/cast lookup, token -> primitive-op maps, scope API pass-through |
| `LoweringContext.h` | Per-function state: the record, unit, enclosing type, slots, `BindLocal` / `LocalValue` (a local's slot lives on its scoped symbol), label stacks, defer-body depth, the jump scopes, coercion helpers, the diagnostic channel |
| `ExprLowerer.{h,cpp}` | Expression walk: `ResolveExpr(node) -> QueryResult<Value>`, one method per AST expression node |
| `StmtLowerer.{h,cpp}` | Statement dispatch, blocks (`RegionScope`), return, if, switch, break / continue / goto / label |
| `DeclLowerer.{h,cpp}`, `DeclBraces.cpp` | Declarations: slots, reference locals, direct construction, braced starts (fields, designators, array elements in index order, ranges), value-local construction and destruction, static locals (`Once` + `AtExit`), globals (`StartGlobal`), braced values outside a declaration (`BracedValue`) |
| `GlobalInit.{h,cpp}` | One round of `__c4_global_init`: the globals and static members not started yet that start at run time, in declaration order (a template instance's with its bindings) |
| `ConstantImage.{h,cpp}` | The data a global starts holding when its lowered start only stores constants (numbers, `null`, string literals) into fixed parts of it: such a start becomes the global's initial value instead of code in a round (`CodeGen/DataImage` writes it for the storage's LLVM type) |
| `PackElements.{h,cpp}` | The copies of a pack expansion: `Within(i, lower)` lowers with each pack the pattern names meaning its element `i` (D-19) |
| `FoldLowerer.{h,cpp}` | Fold expressions: the copies combined from the right or the left with `ApplyBinary` / `ApplyShortCircuit`, a comma fold as a sequence, the empty-pack values |
| `TemplateBindings.{h,cpp}` | `BindTemplateParameters`: a template's bindings (types, constants, packs) as names in the current lowering layer |
| `ExprAssignment.cpp` | `ExprLowerer`'s assignments: `ResolveAssignment`, `AssignTo` (an already lowered value), `AssignValue` (a lifecycle assignment or a store), `CompoundAssign`, `StoreAssigned` (including a by-value indexer's setter) |
| `LastUseMoves.{h,cpp}` | D-05's last-use moves: after a body lowers, each `TransferNode` (a copy of a local or by-value parameter) that is its variable's last use and whose address never escaped becomes a move, with a drop flag guarding the variable's cleanup |
| `DestructuringLowerer.{h,cpp}` | `[a, T b, _] = value;`: the value held once in a statement temporary, each element moved out of it to a new local (`DeclLowerer::DeclareValue`), an assignment, or discarded |
| `LoopLowerer.{h,cpp}` | while, do-while, for, range-for (arrays, pointers, init lists, the iterator protocol) |
| `ExceptionLowerer.{h,cpp}` | try/catch, throw, defer; `finally` as a defer around the try |
| `JumpScopes.{h,cpp}` | The goto rules: the block path of every label and goto, checked after the body |
| `IndexLValue.{h,cpp}` | The element lvalue of a built-in `base[index]` |
| `MethodReceiver.{h,cpp}` | A method call's receiver: moved to the sub-object that declares the method, or to the vptr that holds its slot (`BindReceiver`, `CallDispatch::Direct` for `Base::F()`); `SubobjectLValue` |
| `IndirectCall.{h,cpp}` | A call through a funcptr: argument count, conversions to the parameter types, reference arguments |
| `ReadOnly.{h,cpp}` | `ReadOnlyReason(ctx, lvalue)`: why a location cannot be written (a const local, parameter or global, a location behind a pointer or reference to const, a const field outside its object's create / copy / delete, `this` in a const method, an element of a const array), and `AddressType` (`&x` of a read-only x is a pointer to const) |
| `PointerConversion.{h,cpp}` | `CheckPointerConversion`: the one rule for a pointer that becomes another pointer, shared by assignment, `InitialValue` (declarations, arguments, returns) - the same pointee, null, `void*` / `void**` either way, a derived-to-base upcast, never dropping const |
| `LoweringDriver.{h,cpp}` | `LowerFunction` |

`ExprLowerer` is one class whose member definitions are split by concern: `ExprLowerer.cpp` (dispatch,
names, `c ? a : b`, lambdas, default arguments), `ExprAccess.cpp` (members, indexing, static and enum
members), `ExprCalls.cpp` (calls: the callee, the fallbacks when no function matches, arguments, defaults, the
receiver), `ExprCallTargets.cpp` (what a callee names: a function by name, a method, a qualified name, a value), `ExprConstruction.cpp` (`new`, construction,
braced and initializer lists), `ExprOperators.cpp` (unary, binary, `&&` / `||`, operator calls),
`ExprCasts.cpp` and `ExprAssignment.cpp`.

## Values and lvalues

The rvalue/lvalue distinction is first-class:

- An **rvalue** is a node that produces a quantity.
- An **lvalue** is a compact `LValue` descriptor in the function arena, referenced by pointer, so nested
  chains (`this->inner.x`) stay valid. Kinds: `Slot` (param/local/temp/static), `Field` (base lvalue +
  field; the field's offset is relative to its declaring type, so an inherited field's base is a `Base`
  view), `Base` (the base sub-object of `base` at `baseOffset`), `Global` (model symbol), `This`, `Deref`
  (`*ptr`), `Lane` (one packed SIMD lane), `Swizzle`
  (multi-lane run with reordering/repeats, packed or unpacked), `VectorLaneDyn` (`v.data[i]` on a packed
  array alias), `IndexOp` (an overloaded `obj[i]` that returns by value: read calls the getter, write calls
  the `[]=` setter).
- `LoweringContext::LoadIfNeeded` is the ONE place an lvalue becomes an rvalue (a `LoadNode`).
- Write legality is tracked on the chain: `ephemeral` (rooted in a by-value call result, so a store would
  vanish - rejected) and `constRooted` (passes through a `const T*` or const object - rejected).
  `ExprLowerer::ChainBlocksWrite` reports which link blocks the write.

Codegen walks lvalues to produce loads, stores, GEPs, `extractelement`/`insertelement`, or getter/setter
calls; HIR never computes addresses.

## Node taxonomy

Expression-like nodes: `Constant`, `Load`, `Call` (direct via record, indirect via a funcptr or func target, or
virtual via slot index - the receiver is then the sub-object whose vptr holds the slot; flags for operator
calls and by-value-result materialization), `DynamicCast` (checked `as` / `dynamic_cast` / `is` on a
pointer to a polymorphic object), `IntrinsicOp`
(primitive `+ - * / %`, comparisons, bitwise, shifts, `Negate`, `BitNot`, `BoolNot`), `Convert`, `New`,
`Construct` (temporary `T(args)` / `T{...}`), `Delete`, `Relocate` (`relocate(dst, src)`), `AddressOf`,
`AddressArithmetic` (byte offset), `Ternary` (also used for short-circuit `&&`/`||`), `InitList`
(`__init_list<T>`), `FunctionAddress` (function-to-pointer decay, a capture-less lambda's call method),
`MakeFunc` (a `func<R(A)>` from null, a funcptr, or a closure: a GC copy of it, or for a one-pointer closure
the pointer), `FuncCode` (a func's code pointer, for `== null` and `!f`), `VectorBuild` (braced write to a
packed run), `Sequence` (ordered steps yielding the last one: unpacked runs, copies, `take(x)`, the end of a
full expression; `fresh` marks a new object the consumer owns; a `Block` step is statements run in place,
such as the loop that copies an array or fills a braced one in a temporary). `New` carries `placement` for
`new (p) T(...)`.

Statement-like nodes: `Block`, `If`, `While` (incl. do-while), `For`, `Switch` (constant case values,
per-case entry labels for `fallthrough`), `Break`, `Continue`, `Jump`, `Goto`, `Label`, `Return` (carries the
function's return type), `ExprStmt`, `LocalDecl`, `Store`, `Try` (try block, catch clauses: the caught
value type, by value or by reference (the slot then holds the address), const, or catch-all with no
slot), `Throw` (value or rethrow; carries the exception object's copy and destroy records), `Defer` (a
scope-exit cleanup; `exceptionalOnly` runs it only when an exception unwinds the block), `Once` (a region
that runs the first time control reaches it, once per program, threads waiting for it; an exception
abandons it so the next pass runs it again), `AtExit` (registers an object with static storage to be
destroyed at exit: its delete, an element count and stride), `Asm` (inline assembly, `AsmLowerer`: LLVM's template
and constraint string, the input values, and the slots its output registers are read into; the statements after it
store each slot into its operand's location, converted from the register's type).

Slots have a class: `Param`, `Local`, `Temp`, `Static` (static locals become module globals in codegen; a
static slot whose `initial` constant is set starts holding it).

## Lowering rules

The rules worth knowing when changing the lowerer (each is one place in the code):

| Construct | Lowering |
|---|---|
| Identifier | `QueryVariable` with the context's `enclosingType` and `scope`: local, then member of `this` (incl. packed lane names), then statics of enclosing types, then globals. Constants fold to `Constant`. |
| `a.b`, `p->b`, `p.b` | `QueryMember` on the base type; `.` on a pointer auto-dereferences. An inherited field is reached through the `Base` view of its declaring sub-object. Private field access from outside the type is rejected. |
| Method call | `BindReceiver`: the receiver moves to the declaring sub-object; a virtual method dispatches through the vptr `VirtualTables::DispatchFor` names (slot index on the node). `Base::F()` is direct. A method found in two unrelated bases with one signature is ambiguous. |
| Writes and const | An assignment, `++` / `--`, `take(x)`, binding a non-const reference parameter or local, returning a non-const reference: the location must not be read-only (`ReadOnlyReason`). A method or operator call passes the receiver's access (`ExprLowerer::AccessOf`; an unqualified call in a method uses `this`'s, `ImplicitReceiverAccess`) so a read-only receiver resolves only const methods. |
| Binary/unary operator | Primitive operands -> `IntrinsicOp` after the usual arithmetic conversions; otherwise `QueryOperator` -> `CallNode` (receiver on lhs or rhs). Compound assignment prefers `<op>_assign`, else `a = a op b`. Post-increment yields the old value. |
| `a.operator_xxx(...)` | Ordinary `QueryFunction("operator_xxx", receiver)`; the annotation set resolves it. |
| Call | `QueryFunction` with argument types; defaults filled from the record; arguments bound with `BindArgument` (`InitialValue`); a reference parameter binds the argument's address (`AddressAs`: converted to the base sub-object for a derived argument; non-const ref to const object and ref to prvalue are rejected). A callee that is a value - a variable, a field, a call result, a lambda - is called as one (`CalleeFromValue`): a funcptr or func through `LowerIndirectCall`, an object through its `operator_call`. |
| `T v;` / `T v(args);` / `T v = {...};` | Zero-initialize the slot, then `PlanConstruction` (see [Value lifecycle](#value-lifecycle)) or fill fields/elements from the braced list (nested aggregates, designated `[i] =` and ranges) inside a fill guard. The variable's destruction is scheduled as a scope-exit defer after its initialization. |
| `static T v ...;` (a local) | `DeclLowerer::LowerStaticLocal`: the slot is `Static`; its whole start (initializer, `(args)`, braces or default construction, with the statement's temporaries) goes into a `Once` body, after an exceptional-only `Defer` that zeroes the storage again when the start is abandoned, and ends with an `AtExit` when the type needs destruction. A body that is only a constant store becomes the slot's `initial` value instead. A static reference stores the address once. |
| Globals and static members | `LowerGlobalInit` (`GlobalInit.cpp`) builds one round of `__c4_global_init`: the variables not started yet that do not fold to a constant, sorted by unit and source position, each lowered with `DeclLowerer::StartGlobal` in its unit and declaring scope (a template instance's static with the instance's bindings), then `AtExit`. A start that only stores constants (`ConstantImage`) leaves the round and becomes the global's data. A failed initializer is a lowering error. |
| Pack expansions and folds | An expansion in call or construction arguments, an init list or a braced fill is its pattern lowered once per element (`PackElements`); a braced fill lowers each copy where it lands, so nested braces work. A fold (`FoldLowerer`) combines the copies with the binary operator rules; `sizeof...` is a constant. A pack named without `...` is an error. |
| Multiple return values | `return [e0, e1];` is the return list built like `T{e0, e1}` (`ExprLowerer::ResolveValueList`, the count must match); a value list on the right of a destructuring is the list of its values' own types. A destructuring holds the value once, then takes each element out (relocation for an object) in order. |
| A template's name as a value | `ExprLowerer::ResolveFor(node, target)` (initializers, assignments, returns, braced entries) and the call arguments (bound after overload resolution, `BindTemplateNames`) make a function template's name the address of the instance its funcptr/func target deduces. |
| `T v = expr;`, arguments, returns, `throw` | `InitialValue`: a fresh object relocates, a view is copied (`Lifecycle::CopyOf`), a callable converts to a funcptr or func target (`Callables.cpp`: `ConvertCallable`); `BindArgument` does the same per parameter. `return local;` relocates and the local is not destroyed. |
| `new T(...)`, `new (p) T(...)` / `delete p` | `NewNode` with the plan and the full destruction as finalizer (none for placement) / `DeleteNode`. |
| Casts | `[operator("cast")]` (member or primitive-receiver extension) wins; else `ConvertNode` (codegen moves pointers between related objects to the sub-object). `static_cast` / `reinterpret_cast` / `const_cast` lower like a C cast; `dynamic_cast<T*>` is `as T*`. |
| `x as T*`, `x is T` | An upcast is a `ConvertNode`; a downcast or cross-cast of a pointer to a polymorphic object is a `DynamicCastNode` (`testOnly` for `is`); `is` on anything else is a constant from the static types. Numeric `as` and casts between unrelated types are errors. |
| Lambda | `LambdaLowerer`: the captures are planned against the enclosing scope (`LambdaCaptures.cpp`: `FindOuterUses` lists what the body uses from outside; explicit captures, `[=]` / `[&]` defaults, init captures, `this` / `*this`), the model builds the closure type (`ProgramModel::CreateClosure`), the body lowers at once as its call method (a nested `HIRFunction`; static when nothing is captured), and the value is a `Construct` of the closure from the captured values. In the call method (`EnterClosureBody`) each capture is a name bound to its closure field (`SymbolInfo::localCaptureField`; by-value captures read-only unless `mutable`), and a captured `this` is what `this` and members name (`LoweringContext::ReceiverLValue`). |
| Range-for over array | Desugared to an index loop; the loop variable is declared in the body (a copy of the element, destroyed each iteration). |
| Range-for over a type | `begin`/`end` operators, then `while (it notequal end) { T v = *it; body; ++it; }` using `notequal`/`dereference`/`increment`; the iterator step assigns through `Lifecycle::Assign`. Missing bodies are named in the error. |
| `switch` | Case values must fold to constants; implicit C fallthrough plus explicit `fallthrough;`. |
| `defer` | `DeferNode` with its own region; codegen runs it on every exit edge of its scope, LIFO. A defer body may not return, goto, hold labels, or break/continue out of itself. |
| Local type / alias / function | Registered through the model's local-declaration API; local functions become nested `HIRFunction`s. |
| `try`/`catch`/`finally`, `throw` | `TryNode` with clauses in source order (each catch parameter is a slot local to its clause's scope; a reference parameter's slot holds the exception's address and the name reads through it; `catch (...)` has no slot); `finally F` becomes a wrapper block `{ defer F; try ... }`; `ThrowNode` value or bare rethrow. |
| `sizeof(T)`, `__uuidof(T)` | Constant. |
| `take(x)`, `destroy(p)`, `relocate(dst, src)` | `Intrinsics.cpp`, tried only when no function of that name exists: `Lifecycle::Take`, `Lifecycle::DestroyCall`, `RelocateNode`. |

## Value lifecycle

Construction, copy, assignment, relocation and destruction (docs/language/06, D-03..D-07) are one
subsystem in `Backend/HIR/`; every other lowerer calls it instead of storing objects directly:

- `LifecycleFacts` - what a type needs: its own lifecycle methods (`Own`: creates, 0-argument create,
  delete, copy, `=`), and whether it or any part (class base, fields, array elements) needs construction,
  destruction or a copy. `CheckCopyable` rejects copying a type with a delete and no copy.
- `Lifecycle` - the HIR for each step. The function of a step is the type's own method or a
  compiler-written one (`ProgramModel::EnsureSynthesizedLifecycle`: `__c4_create/delete/copy_<T>_<n>`
  records with `synthesized` set and no body node; `LowerSynthesized` writes their bodies), and every call
  to one is direct. `Construct` / `DestroyNow` / `ScheduleDestroy` / `CopyInto` work on lvalues; `CopyOf`
  and `Take` yield fresh values; `Assign` implements D-06; `Temporary` gives a fresh object an address for
  the rest of its statement.
- **Fresh and view values.** `Lifecycle::IsFresh`: a by-value call result, `T(...)`, a copy, `take(x)` or an
  object `?:` is a new object the consumer owns, so it relocates (bytes moved, nothing destroyed); any other
  object value is a view of an existing object and is copied when it must be owned.
- `MethodBody` - the parts a create, delete or copy gets around its body: `ConstructParts` (base unless the
  body's first statement calls a base create, then the fields), `ScheduleDestroyParts`, `CopyBase`, and
  by-value parameter destruction for every function.
- `Construction` - `PlanConstruction` picks what `T v(args)`, `T(args)`, `T{...}` and `new T(...)` run:
  the init-list create, the default create, overload resolution over the type's own creates, a whole-object
  copy (`T(x)` with `x` a `T`), a lane fill for a pure packed vector, or an aggregate fill. When a field
  needs destruction, the aggregate's arguments are held in temporaries in `plan.prelude` first.
- **Temporaries.** `Lifecycle::Temporary` and `Hold` register a pending temporary; `StmtLowerer::LowerStmt`
  moves the statement's nodes into a block that zeroes each temporary first and destroys it at the end
  (cleanups the statement registers for the enclosing scope stay outside), and
  `StmtLowerer::FullExpression` ends the temporaries of a condition or loop step right after it is
  computed (`Lifecycle::EndFullExpression`: destroy, then zero).
- **Exception safety.** `Lifecycle::PartsGuard` / `FillGuard` wrap a create, a copy or an initializer-list
  fill in a block with an `exceptionalOnly` defer that destroys the parts built so far.

## Scopes and labels

- Every block pushes a model scope layer and pops it by RAII at the end of its own lowering, including when
  an error aborts the function. A nested `{ }` is its own child `BlockNode`, so shadowing is real nesting.
- A local is registered AFTER its initializer is lowered, so `int32 x = x;` cannot see itself.
- Control-flow bodies are child blocks owned by the control node, not siblings in the parent region.
- `break` targets the innermost loop or switch and `continue` the innermost loop (two independent label
  stacks in `LoweringContext`). Using either outside a valid target is an error.
- Every block a body lowers into is entered through one `RegionScope`, which pushes the model scope layer
  and the block's place on the jump path, so lookup scoping, the goto rules and codegen's frames agree.
- A `for` statement's init clause is its own block around the condition, body and step.
- `goto`/labels share one id per name regardless of order. After the body is lowered, `JumpScopes::Validate`
  checks every goto: undefined label, a label in a nested block, or a forward jump across a declaration or
  defer of the label's block are errors.

## Diagnostics

- One channel: `LoweringContext::Fail` / `Report` write to the build's `ErrorTracker`. The first failure in a
  statement aborts that statement and is its only report (the layers the failure propagates through add
  nothing); the statements after it still lower (`StmtLowerer::LowerEach`), so a function reports one error per
  failed statement (BUG-361), and its tree stays incomplete. Other functions keep lowering, so one run
  reports errors from many functions. `CodeGenDriver` lowers every body before emitting any; if any
  lowering error exists it stops and reports them (at most the first 16, prefixed `lowering:`) instead of
  emitting a partial module.
- Messages come from the model's `QueryResult` where the model detected the problem, with the AST node's
  location as fallback. Examples of what HIR rejects: `no variable named y`, `no matching overload of Add`, `no member
  'push_back' in 'Core::Array<int32>'; did you mean 'Push'?` (MemberSuggestion: the C4 name of a C++ standard library
  member, or a close spelling),
  `Cannot access private field 'f'`, `cannot write through this location: ...`, `cannot bind a reference to a
  prvalue`, `'break' with no enclosing loop or switch`, `missing return value for non-void function`,
  `index assignment on a by-value-only indexer` (with a fix suggestion), `iterator protocol: 'end' has no
  body`, `cannot initialize ... dropping const is not allowed`.
- Internal fallbacks (`expression not yet lowered`, `statement not yet lowered`) indicate a lowering gap,
  not a user error - each occurrence is a bug to fix.
- The LSP runs this same lowering for its diagnostics, so the editor and `C4.exe` cannot disagree.

**Partial:** const methods, private/protected methods, protected fields and abstract-class instantiation
are not enforced yet. See [../status/known-bugs.md](../status/known-bugs.md).
