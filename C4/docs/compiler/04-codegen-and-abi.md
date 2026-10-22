# 04 - Code generation and ABI

The backend (`C4Lib/src/Backend/CodeGen/`) turns HIR trees plus the model's layout facts into ONE
`llvm::Module` (vendored LLVM 22.1, opaque pointers). It is a "dumb emitter" by design: it never resolves a
name, an overload or a type. If codegen needs a semantic fact the HIR node does not carry, the fix is to
put the fact on the node, not to compute it here.

## Components

Every component is a 1:1 `.h/.cpp` pair; `CodeGen.h` is the umbrella.

| Component | Lifetime | Responsibility |
|---|---|---|
| `CodeGenDriver` | one compile | Owns the `ModuleContext` and the lowered HIR trees; runs the assembly order below. Entry: `Lower(model, entryName, errorMessage, runTests)`. It remembers which emitted tree first referenced each new function, so an error in a template-made body ends with the instance and that use (`UseSites`: `(in 'Core::Array<Net::TcpStream>::Push', used at main.cf (12:5))`). |
| `ModuleContext` | one compile | The module; the single C4 -> LLVM type map (`Lower`); struct creation/filling (`StructFor`, `FillStruct`); function signatures (`GetOrCreateFunction`, `SignatureOf`); global storage (`GlobalAddress`); runtime entry points (`RuntimeFunction`). Owns the `Abi` brick and the object-model emitters below. |
| `Abi` | one compile | THE calling-convention brick: which types are by-value aggregates, register vs hidden pointer, sret, the LLVM param/return types at a boundary. |
| `IRContext` | one function | The `IRBuilder`, slot allocas, `this`, entry-block temporaries, the `CleanupStack`, the unwind-target query, and the ONE call brick (`Call`: funclet bundle + invoke). Truthiness rule. |
| `CleanupStack` | one function | The scope-exit obligations (cleanups), try scopes and catch handlers between the entry and the current point, in nesting order. |
| `FunctionEmitter` | one function | Entry block, slot allocas, parameter binding, runs the region walker, terminates leftover blocks, emits nested functions as siblings. |
| `RegionEmitter` | one function | Statements and control-flow blocks, block frames and labels, scope exit (`ExitCleanups` / `BranchThroughCleanups`), try/catch, throw, and the lazily created cleanup pads (`CleanupPadFor`). |
| `ExprEmitter` (+ `ExprOperatorEmitter.cpp`: operators and conversions) | one function | Rvalue nodes -> SSA values; lvalue -> address resolution (incl. base sub-objects); loads/stores incl. SIMD lanes; conversions (object pointers move to the target sub-object); checked casts; the one store helper (`GcStore`). Creates blocks only for ternary and short-circuit joins. |
| `CallEmitter` | one call | Hidden parameters in MSVC order, each argument per `Abi`, the callee (direct, virtual through the receiver's vptr, indirect through a funcptr), the result unpacked to its C4 type. Constructor calls share it. |
| `AsmEmitter` | one `Asm` statement | a call of `llvm::InlineAsm` (Intel or AT&T dialect, side effects unless `pure`, stack alignment unless `nostack`, `memory(none)` / `memory(read)` for `nomem` / `readonly`); outputs extracted and stored into their slots; `noreturn` ends in `unreachable`. File-scope `asm` is module assembly (`CodeGenDriver::EmitFileAssembly`), each block wrapped in `.intel_syntax noprefix` / `.att_syntax prefix` unless it is AT&T. |
| `ObjectEmitter` | one expression | `new T(...)`, `new T[n]`, `T(...)` temporaries, `__init_list<T>` arrays, `delete p`: storage, vptrs, then the constructor or the field fill. |
| `VtableEmitter` | one compile | Vtables of complete objects (one per vptr location), this-adjusting thunks, `c4_purecall` for unimplemented slots, and the vptr sites of any type (`StoreVptrs`, `ZeroImage` for statics). |
| `RttiEmitter` | one compile | MSVC RTTI: TypeDescriptors (by decorated name), ClassHierarchyDescriptors with their base class arrays, CompleteObjectLocators. |
| `EhTables` | one compile | ThrowInfo / CatchableTypeArray per thrown type; the catchpad operands of a clause. |
| `MsvcNames` | - | MSVC decorated type names (`.H`, `.?AVFoo@Ns@@`, `.PEAX`), the identity RTTI and EH compare. |
| `PeTables` | - | Image-relative offsets and shared / private read-only tables for RTTI and EH. |
| `AliasClass` | one compile | The strict-aliasing class (D-64) of a type and of an access: bytes, bool, 16-, 32-, 64-bit integers, float, double, pointer; an object, array, vector or union is its parts' class when they share one, else bytes; an access through a union member of another class is bytes. |
| `AliasTags` | one compile | The `!tbaa` nodes of the module (root `C4 TBAA`, `bytes` the parent of every other class) and an access tag per class; off with `-fno-strict-aliasing`. |
| `DebugInfo` | one compile (`-g` only) | The CodeView description of the module through `DIBuilder`: the compile unit and files, a subprogram per written function, parameters and locals at their slots, the C4 types, globals, and the line of each statement (see below). |

## Assembly order (`CodeGenDriver::Lower`)

`Lower` is one call per step: `LowerTypes` (1), `DeclareWrittenFunctions` (2), `EmitReachable` (4), the
`TestHarnessEmitter` (6) or `EmitEntryPoint` (7), `EmitReachable` again for what those reference,
`EmitStaticState` (invoke thunks, 5 and 3), `EmitReflection` (the reflection image), `FinishModule` (8, 9) and
`Verify` (10).

1. **Types.** Lower every object/union to a named struct, created opaque (so recursive and incomplete types
   are legal) and filled from the model's field offsets.
2. **Function declarations.** `GetOrCreateFunction` for every record written as it is, so forward calls,
   mutual recursion and runtime / interop names (declared once, by their record) work. Template-made and
   compiler-written functions are declared on first reference. Declaration and body share one signature
   builder, so their types are identical.
3. **Globals.** Storage for a global is made when code first names it; after global initialization
   (step 5) every top-level variable gets its storage, whether or not code names it (reflection registers
   each). `[link("sym")]` variables
   become external declarations of `sym` with no storage (two declarations of one symbol share it, as do
   two function declarations of one foreign function). Function-pointer initializers resolve to `@F`.
4. **Bodies.** `EmitReachable`: HIR-lower and emit every record written as it is that has a body, then,
   until none is left, every template-made or compiler-written function the emitted code referenced for
   the first time (`ModuleContext::TakeNewFunctions` - calls, function addresses, constructors, finalizers,
   vtable entries, EH copy and destroy functions). A class-template member nothing references is never
   lowered (D-16). Any lowering error aborts the build with the diagnostics; the closure runs again after
   the test harness and the entry point.
5. **Global initialization**, after the bodies, the test harness and the entry point, so every template
   instance they made is known: `__c4_global_init` (`CodeGenDriver::EmitGlobalInit`, HIR from
   `Backend/HIR/GlobalInit`), registered in `@llvm.global_ctors` (priority 65535, after C++ static
   constructors), calls the rounds `__c4_global_init_round<n>` in order. Every global and static member that
   does not fold to a constant starts in a round, in declaration order (units in build order, each by source
   position), each in its own defining unit and declaring scope, through the same `DeclLowerer` code as a
   local (`StartGlobal`); one that needs destruction is followed by an `AtExit`. Each round is emitted with
   what it reaches (`EmitReachable`); a template instance first made there has its static data members
   started by the next round. Not emitted when nothing starts at run time. Then `MaterializeGlobals` gives
   every global its storage, and the reflection image is emitted with its registering constructor
   (`EmitReflection`, priority 65534: before every other static constructor).
6. **Test harness** (`-test`, see below).
7. **Entry point.** Emit `@main` forwarding to the user's entry (`main_0` etc.), mirroring its
   `argc/argv` parameters and returning its value; a `void` entry returns 0. A `-test` build needs none.
8. **GC roots** (`GcRoots.cpp`). `__c4_gc_roots`: `{address, size}` of every global, static member and static
   local whose type may hold a managed pointer (`TypeHandle::MayHoldPointers`), ended by `{null, 0}`. It is
   emitted last, so the static locals the bodies created are listed.
9. **Vector alignment and the target.** `AlignVectorAccesses` gives each load and store of a vector whose size is
   not a power of two the alignment C4 guarantees; `ApplyTarget` (`TargetAttributes.cpp`) gives every function the
   module defines the build's `target-cpu` and `target-features` (D-62, from `ProgramModel::Target`), as clang
   does, so the optimizer, the inliner and the object writer all use the build's processor. A function annotated
   `[target(...)]` then gets the build's features with its own on (`ApplyFunctionTargets`, `CpuTarget::With`).
   **Debug information** (`-g`): `DebugInfo::Finish` completes the metadata (the subprograms, variables and
   globals were described as their functions and globals were emitted).
10. **Verify.** `verifyFunction` per function (the diagnostic names the culprit), then `verifyModule`.

## Type mapping (`ModuleContext::Lower`)

| C4 type | LLVM type |
|---|---|
| `bool` | `i1` |
| `int8..int64`, `uint8..uint64`, `char`, `char16`, `char32`, `snorm*`, `unorm*` | `iN` of the storage width (signedness lives in the model and selects `sdiv`/`udiv`, `ashr`/`lshr`, extensions) |
| `float` / `double` | `float` / `double` |
| `T*`, `T&`, `funcptr<...>` | opaque `ptr` |
| `func<R(A)>` | `{ ptr code, ptr environment }` |
| `T[N]` | `[N x T]` |
| class / struct / interface | named struct with each piece at its model offset: own vptr, base sub-objects (nested structs), fields; statics excluded, each anonymous-union region one slot |
| union | `[N x i8]` |
| enum | its underlying integer |
| pure packed vector / packed sub-vector | `<N x T>` |
| `__init_list<T>` | `{ ptr data, i64 size }` (the `std::initializer_list` layout) |

Opaque-pointer consequences: every load/store/GEP names its element type from the HIR node's concrete type,
and pointer "casts" emit nothing - except conversions between pointers to related objects, which move the
address by the base's offset (`ClassHierarchy::PointerDelta`) and keep `null` as `null`. A global or
`static` whose type has vptrs is created with a packed constant image of its zeroed bytes plus the vtable
pointers, so its LLVM value type differs from `Lower(T)`; every access goes through byte GEPs anyway.

Normalized integers (`snormN`/`unormN`, D-38): `CodeGen/NormalizedConversion` encodes a float (clamp,
scale, `llvm.rint`, with NaN to 0), decodes to a float (encoding / scale, a snorm clamped at -1), converts
between normalized types by value, and clamps an integer into the encoding range. `ModuleContext::
NumberConstant` encodes normalized constants the same way. Arithmetic never reaches codegen on the encoding:
the usual arithmetic conversions make it float arithmetic in HIR.

## Windows x64 ABI

The target is fixed to `x86_64-pc-windows-msvc`, and the rules must match clang byte for byte so C4 objects
link with clang-compiled C/C++ in both directions. All of it lives in `Abi`:

- Scalars (ints, floats, pointers, funcptrs) pass and return in their own register.
- A by-value aggregate (object, union or array) of exactly **1, 2, 4 or 8 bytes** passes and returns as one
  integer of that width.
- Any other aggregate size passes by **hidden pointer** (the caller materializes a temporary and passes
  its address) and returns via **sret** (a hidden out-pointer, LLVM return type `void`).
- References lower to pointers; a reference return is a pointer return, never sret.
- A pure packed vector and a `packed T[N]` pass and return as their `<N x T>` vector, as clang passes `__m128`,
  `__m256` and `__m512`: LLVM's Win64 convention passes a vector argument by hidden reference and returns it in
  XMM0 / YMM0 / ZMM0 when the target has the register, split into XMM registers when it does not. That is why one
  program has one target (D-62): the host, its foreign objects and its scripts must agree on it.
- Hidden parameters (`Abi::SignatureOf`, the one source for declarations, prologues, call sites and
  thunks):
  - **Member function** (a method of a C4 type): `[this] [sret?] [params...]`, and **every** object or
    union return goes through sret, whatever its size (the MSVC member convention). This is what lets C++
    call methods of C4 objects through an interface, and C4 call C++ virtuals.
  - **Extension method** (incl. CBind's bindings of C++ methods, which call `extern "C"` wrappers):
    `[sret?] [this] [params...]` with the free-function return rules - the receiver is an ordinary
    leading argument of a C function.
  - **Free / static function, funcptr call:** `[sret?] [params...]`.
- `FunctionEmitter` reinterprets register-passed aggregates back into the object slot and loads
  hidden-pointer aggregates through the pointer; the sret pointer is `IRContext::Sret()`.
- `-mstackrealign` is always passed to clang.

The model is the size authority (`Type::GetSize`); `Abi` only classifies. Interop tests under
`assets/IsolatedTest/Interop/` (by-value aggregates of each size class, struct-by-pointer, C++ references,
init lists, COM) are the oracle for this section.

## Objects, vtables and construction

The object model follows the MSVC x64 ABI ([../language/05-objects.md](../language/05-objects.md)). The
model owns the facts, in two bricks shared by HIR and codegen so they cannot disagree:

- `Model/Types/ClassHierarchy` - where each base sub-object sits (`LocateBase`, `PointerDelta`), the
  sub-object walk (`ForEachSubobject`, `Subobjects` in base-class-array order with ambiguity), field
  lookup across bases (`FindField`), and the base-list rules (`CheckBaseList`, `CheckInterfaceMembers`).
- `Model/Types/VirtualTables` - slots identified by name plus parameter types (`PrimarySlots`: the
  primary base's slots in order, then new virtuals), every vptr of a type (`VptrLocations`), the method a
  slot runs in a complete object (`FinalOverrider`, with its sub-object offset), and how a call dispatches
  (`DispatchFor`: which vptr, which slot).

Emission:

- A complete object of type `T` has one vtable per vptr location: an internal global
  `__c4_vtable_<type symbol>_<vptr offset>` (named for the type as every program names it, so a script uses its
  host's, [runtime/04](../runtime/04-scripting.md)) holding `{ CompleteObjectLocator*, entries... }`; the vptr
  points at entry 0, so the locator is `vtable[-1]` as in MSVC. A `--script-host` build emits the vtables of every
  class with virtual methods, made or not. Entry `i` is the final overrider of slot `i`. When the overrider's `this` is a different
  sub-object than the vptr's (an interface implemented by the class), the entry is a thunk
  `c4.thunk.<fn>.<delta>` that moves `this` and tail-calls the method. A slot with no implementation calls
  `c4_purecall` (prints "pure virtual method called", exits 1).
- HIR moves a virtual call's receiver to the sub-object whose vptr holds the slot; `CallEmitter` loads
  that vptr from the same `this` it passes (the receiver is evaluated once) and calls entry `index`.
- Every vptr of an object is written where the object comes into existence: stack slots at function
  entry, `new` and `T(...)` before the constructor runs, globals and `static` locals through their
  constant initializer. Nested members and array elements are included (`VtableEmitter::VptrSites`).
- A load of a whole polymorphic object through a pointer, `this` or a base view rewrites the copy's vptrs
  to its static type's tables (slicing); a store through those keeps the destination's vptrs.
- RTTI (`RttiEmitter`): `TypeDescriptor` `??_R0<name>@8` (linkonce_odr, writable, null `type_info`
  vftable: matching compares the address or the decorated name), `ClassHierarchyDescriptor` + base class
  array (every sub-object, MSVC order and attributes), `CompleteObjectLocator` (signature 1, image-relative).
  `C4Runtime/RttiRuntimeC.c` walks them for `is` / `as` / `dynamic_cast`
  (`c4_instance_of`, `c4_dynamic_cast`): complete object from the locator, then the base class array.

## Node emission

| HIR | Emission |
|---|---|
| `Constant` | `ConstantInt` / `ConstantFP`; `null` is `ConstantPointerNull`; string literals are private NUL-terminated globals deduped by content |
| `Load` / `Store` | element-typed load/store with its alias tag (see [Strict aliasing](#strict-aliasing-tbaa)); `Lane`: load vector + `extractelement` / `insertelement` + store; `Swizzle` run: gather/scatter lanes into a `<K x T>`; unpacked runs store field by field; `IndexOp`: getter / setter call |
| `IntrinsicOp` | int vs float chosen by OPERAND type; signedness from the model type |
| `Convert` | ext/trunc/fp conversions/`ptrtoint`/`inttoptr`, normalized encode/decode/clamp; between pointers to related objects `select(p == null, p, p + delta)`; between packed vectors the same instructions on vectors, chosen by the lane types (a bool lane converts unsigned), or a `bitcast` for `keepsBits` (reinterpret_cast between vectors or between numbers of one size) |
| `Simd` | `SimdEmitter.cpp` (D-62): min / max as `fcmp` + `select` for floats (the SSE rule) and `llvm.smin/umin/...` for integers; `llvm.fabs`, `llvm.abs`, `llvm.sqrt`, `llvm.floor`, `llvm.ceil`, `llvm.round`, `llvm.trunc`, `llvm.fmuladd`, `llvm.fma`; `select`; `llvm.vector.reduce.or/and` for any / all, a `bitcast` of the mask for bitmask; `shufflevector` for shuffle, concat and splat; float reductions as a halving tree of shuffles (padded with the identity), integer ones as `llvm.vector.reduce.*`; element-aligned loads and stores, `llvm.masked.load/store/gather` |
| `Call` | `CallEmitter`: direct, indirect through a funcptr, or virtual through the receiver's vptr; hidden params and aggregate args/returns per `Abi::Signature`; created by `IRContext::Call`, which makes it an `invoke` whenever an enclosing cleanup or try must see an exception |
| `DynamicCast` | `c4_dynamic_cast(p, TypeDescriptor)` or `c4_instance_of(...) != 0` |
| `Ternary`, `&&`, `||` | three blocks + PHI, insert point left at the join |
| `New` | `c4_alloc_typed(size, align, name)` (zeroed; `name` the type's QualifiedName for a census) or, for placement, the given storage zeroed with a store; every vptr; constructor (through `CallEmitter`) with `this` = the new pointer or positional field fill; `c4_gc_register_finalizer` with the type's full destruction when it has one (never for placement) |
| `Relocate` | `memmove` of the object's bytes, then the source zeroed with its own vptrs |
| `Delete` | `c4_free(p)` - the runtime runs the registered finalizer once, then frees |
| `Construct` | entry-block temporary, zero-init at the use site, every vptr, constructor or field fill, yields the value |
| `InitList` | entry-block element array + `{ptr, size}` aggregate |
| `For`/`While`/`If`/`Switch` | structural blocks pre-created before the body; every break/continue/fallthrough target is defined with the cleanup depth of its statement; a `for` init block is a frame around the whole loop |
| `Goto` / `Label` | the labels of a block are created when the block starts; a backward goto exits to the depth recorded at the label, a forward goto to the depth of the statement that contains it in the label's block; falling through into a label branches to it |
| `Defer` | pushes a cleanup; see [Scope exit and unwinding](#scope-exit-and-unwinding). An `exceptionalOnly` cleanup is skipped by every normal exit and runs only in its cleanup pad |
| static locals | module globals (value persists across calls), initialized with their vptrs |

A block left unterminated at the end of a body gets `ret void` (or `ret` of a null value for non-void bodies,
which HIR has already rejected for reachable paths) so partial control flow still verifies.

## Scope exit and unwinding

Everything that must happen when a scope is left - a `defer`, a value local's `delete` (HIR emits it as a
defer), a `finally` (HIR lowers it as a defer around the try statement) - is a **cleanup** on the
`CleanupStack`. The stack also holds a **try** entry while a try body is emitted and a **handler** entry
while a catch clause is emitted, so it always describes every scope between the function entry and the
insert point.

- **Normal exits.** Falling off a block pops its cleanups and emits each one inline. Every jump - `break`,
  `continue`, `fallthrough`, `goto`, `return` - knows the cleanup depth of its target and emits the exit
  of every entry above it: a cleanup's body inline, a handler as a `catchret` into a fresh block (leaving
  the funclet before running outer cleanups). The entries are popped while their exit is emitted, so the
  code of a cleanup sees exactly the scopes outside it, then restored for the code after the jump.
- **Return.** The value is computed first, staged in an entry-block slot (or the sret pointer) because SSA
  values do not survive a `catchret`, then every cleanup up to the function scope runs, then `ret`.
- **Exceptional exits.** `IRContext::UnwindTarget` walks the stack downwards: the innermost try gives its
  catchswitch, the innermost cleanup gives its `cleanuppad`, created the first time an exception edge
  needs it (`RegionEmitter::CleanupPadFor`). The pad runs a second copy of the cleanup body with only the
  scopes below it visible (an unwind floor), then `cleanupret`s to whatever is below it. Handlers are
  skipped by the walk: an exception raised inside a handler unwinds to the same place the try's
  catchswitch does, which is how clang structures MSVC funclets.
- **Calls.** `IRContext::Call` is the only place a call instruction is created. It adds the `funclet`
  bundle when the insert point is inside a catchpad or a cleanup pad, and emits an `invoke` whenever the
  callee may throw and an unwind target exists. Runtime calls that cannot reach user code (`c4_alloc`,
  finalizer registration, the `va_*` intrinsics) are plain calls.
- **Temporaries.** Every alloca - slots, sret buffers, materialized arguments, construct temporaries,
  init-list arrays, the return slot, throw buffers - is created at the top of the entry block. An alloca
  anywhere else is a dynamic stack allocation that grows on each loop iteration and breaks clang's frame
  proof under `-mstackrealign`.

## Exceptions

C4 exceptions use the same native Windows EH that clang emits for C++ on MSVC:

- `throw v` stores the value in an entry-block buffer and calls `_CxxThrowException(&v, ThrowInfo)`
  through the call brick (an invoke inside any cleanup or try).
- Every definition gets personality `__CxxFrameHandler3` and `uwtable` up front (an exception-free
  function ignores them).
- A try is a `catchswitch` whose unwind destination is whatever the try itself would unwind to; each clause
  is a `catchpad` `[TypeDescriptor, adjectives, slot]`: adjectives `8` for a reference clause, `1` for a
  const referent or pointee, and `catch (...)` is `[null, 64, null]`. The frame handler copies the value
  (or stores the address, by reference) into the slot. A handler is left with `catchret`.
- Bare `throw;` is the funclet rethrow `_CxxThrowException(null, null)` inside the active catchpad.
- `EhTables::ThrowInfo(T)`: the CatchableTypeArray lists `T` and every unambiguous base sub-object (offset
  in the PMD, the base's size), or for an object pointer every base pointer plus `void*` (simple types,
  adjusted by the PMD); a pointer to const sets the ThrowInfo const attribute. TypeDescriptors are
  `RttiEmitter`'s, named by `MsvcNames`, so C++ and C4 exceptions match across the interop boundary.
- The exception object's lifecycle is the type's own (`EhTables::ThrowInfo` gets an `ObjectLifecycle`):
  each CatchableType's copy function is the copy of that type or base (member convention: `this`, source),
  0 where the bytes suffice, and the ThrowInfo destructor is the type's full destruction.
- A `noexcept` function's body is emitted inside a catch-all funclet (`RegionEmitter::EmitNoexceptBody`)
  whose handler calls `c4_noexcept_violation` (fatal message, exit 1). A call to a `noexcept` callee is a
  plain `call`, never an `invoke`.
- `EHRuntime.cpp` installs a terminate handler: an uncaught exception prints a fatal message and exits 1. Both
  fatal paths report through `c4_report_uncaught_exception`, a function of its own a debugger can stop on.

TypeDescriptor names reproduce MSVC's name back-references (`MsvcNames`): a repeated identifier is its
index in the name's table, and a template-id is mangled with a table of its own that then counts as one
name (`Pair<Box<int32>, Box<int32>>` is `.?AU?$Pair@U?$Box@H@@U1@@@`), so a C++ clause for a template type
matches the C4 one. Windows only.

## GC emission

The runtime contract is in [../runtime/01-memory-and-gc.md](../runtime/01-memory-and-gc.md). Codegen's side:

1. **Stores.** Every typed store goes through `ExprEmitter::GcStore`. The collector needs no barrier, so it
   is a plain store; a future generational or concurrent collector adds its barrier there.
2. **Allocation.** `new` -> `c4_alloc_typed(size, align, name)`, or `c4_alloc_typed_noscan` for a type that holds
   no pointers (`TypeHandle::MayHoldPointers`); `name` is a constant holding the type's QualifiedName (`T[]` for
   `new T[n]`), one per program (a comdat named by its bytes), which the runtime notes only while a census runs;
   finalizer registration when `[operator("delete")]` exists.
   `new T[n]` (`ObjectEmitter::EmitNewArray`, `ArrayStorage`): one allocation of `n * sizeof(T)` bytes (at least
   one, so the pointer to the first element is always inside it). When the elements need destruction an `int64`
   element count comes first, padded to `T`'s alignment, and after HIR's `initialize` block has started every
   element the finalizer `__c4_array_finalizer_<delete>_<per element>_<count bytes>` is registered on the
   allocation: it reads the count and runs the innermost element's delete on each element, last first (a
   `T[2]` element is two `T`). The elements get their vptrs (a loop over the count) before they start.
3. **Explicit free.** `delete` -> `c4_free`.
4. **Roots.** `__c4_gc_roots` for global storage (assembly step 8); everything else is found by the runtime,
   which captures the register file and then scans the stack conservatively, so pointers that optimized
   code keeps only in callee-saved registers are roots too (no per-slot anchors are needed).
5. **Value locals** are not GC objects: their `create`/`delete` are RAII, emitted as scope-exit defers by HIR.

6. **Statistics.** With `-gc-stats`, `CodeGenDriver::InstallGcStatsPrinter` adds a last-priority global
   constructor that calls `c4_gc_install_default_stats_printer()`, so the printer is in place before `main`.

## Test harness (`-test`)

When `runTests` is set the driver emits a synthetic `main` from the model's test registry (never by
re-scanning annotations):

1. global `[test_setup]`;
2. free `[test]` functions sorted by group path;
3. per class (alphabetical): the class's static setup, its tests by group path, its static teardown;
4. global `[test_teardown]`;
5. `c4_test_summary(NULL)`, then return `c4_test_fail_count()` (failed assertions) as the exit code.

Each case is wrapped in `c4_test_begin(name, file, line)` / `c4_test_case_done(name, ms)`. Symbols are
resolved from the recorded base names (`TestFoo` -> `TestFoo_0`, `Counter_TestLocal_0`), preferring the
declaring unit for same-named file-scope statics. A user `main`, if present, is not called. **Partial:** the
harness passes a NULL file and 0 ms, so failures do not show `file:line` and timings are empty; `-test-json`
is not wired. See [../runtime/03-testing.md](../runtime/03-testing.md).

## LLVM API traps (keep these)

Each of these cost a crash or an invalid-IR assert in the vendored LLVM:

1. **`Function::Create` leaves the function empty.** `getEntryBlock()` is `front()` and faults on an empty
   function. Create the entry block first (`FunctionEmitter` and the `@main` wrapper do).
2. **A pointer constant is `ConstantPointerNull`, never `ConstantInt`.** Emitting `null` through the integer
   path produces an invalid operand. Truthiness of a pointer compares against `ConstantPointerNull`
   (`IRContext` has the one truthiness rule).
3. **Pick float vs int instructions from the operand type, not the result type.** A comparison's result is
   `i1` even for float operands.
4. **A ternary needs blocks.** Create then/else/join, emit each arm with its own insert point, PHI in the
   join, and leave the insert point at the join. `&&`/`||` use the same shape.
5. **Binary operands must already share one LLVM type.** HIR inserts `ConvertNode`s; codegen trusts them and
   never assumes a width.
6. **Never assume the builder's insert point.** Every emitter sets it before creating instructions; this
   removes the "instruction landed in the wrong block" bug class.
7. **Funclet exits are special.** Inside a catch or cleanup funclet, leave with `catchret` / `cleanupret`,
   not a plain branch or `ret`; calls inside a funclet need the funclet operand bundle.
8. **Use `poison`, not `undef`,** as the placeholder operand of `shufflevector` and partial vector builds.

## Static storage (`StaticStorage`)

- **`Once`** (a static local's start): a guard `{ i32 state, i32 owner }` named `static.once.<fn>.<name>`.
  The fast path is an acquire load of `state` compared with 1 (done). Otherwise `c4_once_begin(guard)`
  decides: 1 = this thread runs the body (the others wait on a condition variable), 0 = another thread
  finished it. The body runs under an `Abandon` cleanup (`CleanupEntry::Kind::Abandon`: only an unwinding
  exception makes it) that calls `c4_once_abandon(guard)`; a body that completes calls `c4_once_end(guard)`.
  Re-entering a running guard from its own thread is a fatal error in the runtime.
- **`AtExit`**: `c4_at_exit(destroyer, address)`. The destroyer `__c4_exit_<delete>_<count>` is an internal
  `void(ptr)` that calls the type's delete on each element, last first (a loop for an array). The runtime
  keeps the registrations in a list run from one `atexit` hook, last registered first; each destroy runs in
  `c4_run_exit_destroy` (C++), which turns an escaping exception into a fatal error.
- A static slot whose HIR `Slot::initial` is set (a start that only stores a constant) is a global that
  starts holding it (`ModuleContext::ConstantOf`); it gets no guard.
- A `Block` step of a `Sequence` is emitted by the region emitter in place (`RegionServices::EmitNestedBlock`),
  so expression values can be built with loops (an array copy, a braced array, omitted array elements).

## func values (`FuncEmitter`)

A `func<R(A)>` is called like a member function of signature `R(A)` whose `this` is the environment
(`FuncEmitter::CodeType`; MSVC member order: environment, then the sret pointer, then the arguments - so
an object return always goes through sret). `MakeFunc` builds the pair:

- from a closure that captures: the closure's call method, with a GC copy of the closure (`c4_alloc_typed`, or
  `c4_alloc_typed_noscan` for a pointer-free closure; the closure type's delete registered as finalizer);
- from a closure whose only capture is one pointer: `<call>_onepointer`, an adapter that rebuilds the
  closure `{ pointer }` on its own stack and calls the call method - no allocation;
- from a funcptr (a function, a capture-less lambda): `__c4_func_thunk_<signature>`, which calls the
  funcptr held as the environment with the free-function convention, storing a register-returned object
  into the member convention's sret;
- from null: zero.

`func == null`, `!f` and `if (f)` test the code pointer.

## Debug information (`-g`)

`CodeGenDriver::EnableDebugInfo` makes `ModuleContext` own a `DebugInfo` (`CodeGen/DebugInfo`) for the compile;
without it nothing below is emitted.

- **Module.** The `CodeView` and `Debug Info Version` module flags and one compile unit (producer `C4 x.y.z`).
  Each source unit is a `DIFile` from its absolute path (`ProgramModel::SourcePaths`; an in-memory source uses
  its display name).
- **Functions.** A function written in C4 (its record has a body) gets a subprogram named as the program
  spells it (`Ns::Free`, `Type::Method`), with its signature, at its body's line. Compiler-written functions
  (the `@main` wrapper, global init rounds, thunks, EH helpers) get none.
- **Lines.** Every HIR node carries `origin`, the start of the statement it was lowered for:
  `HIRFunction::NewNode` stamps it from `HIRFunction::location`, which `StmtLowerer::LowerStmt` sets per
  statement and restores after it. `RegionEmitter::EmitStatement` sets the builder's location from it
  (`IRContext::At`), so every instruction of a statement, its calls included, carries the statement's line. A
  statement from another file (a template's body) is in a lexical block of that file. A catch handler's entry
  (its catchpad) is at the `catch` (`TryNode::CatchClause::origin`), and so is its caught value.
- **Variables.** After the parameter stores, `DeclareVariables` describes `this` (stored to a `this.addr`
  slot, since it arrives in a register), each parameter, and each local slot the program wrote, at the line
  where it was declared (`Slot::declared`). Compiler temporaries are named `__...` (`__once`,
  `__index_value`, `__checked_cast`, `__cast_receiver`) and are not described.
- **Types.** Primitives with their encoding (a `char8` is named `char`, which makes it CodeView's narrow
  character, so a debugger shows a `char8*` as text), pointers, references, aliases (typedefs), arrays and
  swizzle lanes (arrays of their element), enums with their values, objects as structs with their bases and
  fields and unions with their fields (each cached before its fields, so a field may point back at its type),
  a `funcptr` as a pointer to its signature, and a `func` as `{ code, environment }`.
- **Globals.** `MaterializeGlobals` describes every global and static member once, named as the program
  spells it (`Ns::g`, `Type::s`), at its declaration.

The object is compiled from IR with the metadata, and the link passes `/DEBUG`, so lld writes the `.pdb`
beside the exe. C4Testing `Semantic.DebugInfo` checks the metadata; ToolsTest `DebugInfoFlag` reads a real
`.pdb`; IsolatedTest `-DebugInfo` builds the corpus with `-g`.

## Strict aliasing (`!tbaa`)

D-64 gives LLVM type-based alias information: `ExprEmitter::AccessOf(loc, type)` is a `MemoryAccess` (volatile, and
the tag of the access's class), and `Load` / `GcStore` put the tag on the instruction (`Attach`). Tagged:

- every load and store of a location, whole objects included (a copy of an object whose fields are all floats is a
  `float` access, so InstCombine's split of it keeps the right tag), and the vector load and store of a lane, a run
  or a dynamic lane (the class of the vector's lanes);
- `simd_load` / `simd_store`, their masked forms and gathers (the class of the lanes).

Untagged, so they may touch any memory: zeroing and initializing a fresh slot, vptr loads and stores, `va_arg`,
atomics, call arguments and results, `memcpy` / `memmove` (relocation). A union member is found through the
location: a `Field` of a named union or of an anonymous union region, also behind an array element
(`IndexedArray`). The tags are scalar access tags (`!{!t, !t, i64 0}`); struct-path tags are not needed for the
pointer reloads they remove, and all pointers share one class.

C4Testing `Semantic.AliasTags` checks the classes; IsolatedTest `Types/StrictAliasing*` runs punning that is
allowed (unions, bytes, `memcpy`, bit casts) where the optimizer cannot see that pointers are equal; ToolsTest
`StrictAliasingFlag` checks `-fno-strict-aliasing`.

## Not implemented here yet

- Any C4-side optimization pass (deliberately none; revisit only if a benchmark shows C4 slower than C++).
- Additional targets (Linux, ARM) - **Planned**.
