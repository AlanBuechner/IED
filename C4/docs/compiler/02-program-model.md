# 02 - The program model

`ProgramModel` (`C4Lib/src/Model/ProgramModel.h`) is the semantic layer of the compiler: ONE object per
compilation that owns every type, symbol, constant, template and operator fact of the program and answers
questions about them through a handful of query verbs. HIR lowering, codegen, the LSP and the reflection
emitter all hold the same model. Nothing outside it constructs a type or a scope layer.

## Data structure

| Part | Owner / file | What it holds |
|---|---|---|
| Type store | `TypeStore` | Every `Type*` in the program, owned for the whole compilation (stable pointers). Builtins are interned singletons; `const T*`, `T&`, `T[N]`, `funcptr`, packed sub-vectors (`Swizzle`), `__init_list<T>` and free-function signatures are interned, so pointer identity IS type identity. |
| Symbol table | `SymbolTable` | A scope tree `File -> Namespace -> Class -> Function -> Block`. Each layer maps a name to `SymbolInfo` entries. Also holds per-unit include closures for visibility and the list of all `FunctionRecord`s. |
| `SymbolInfo` | `SymbolTable.h` | What a name denotes: `Type`, `Function` (an overload set of records), `Variable`, or `Namespace`; defining unit, declaration node, initializer, folded constant, mangled symbol, `[link]` symbol, groups. |
| `FunctionRecord` | `SymbolTable.h` | One declaration of a function: signature, enclosing scope, containing type (implicit `this`), primitive receiver (for `extends float`), flags (static, virtual, extension, local, linked, noexcept, `isConst` for a const method - its mangled name
ends in `_const`, so `Get()` and `Get() const` are two functions), `synthesized` (a compiler-written lifecycle function), `templateOrigin` (a class-template member or a function-template instance), body node, `definingUnit` (body lowering context) and `declUnit` (visibility), mangled name, parameter names, `paramIsConst` (from the definition) and default-argument expressions,
operator annotations, groups. |
| Object types | `Model/Types/ObjectType` | Fields (with offsets), methods (`MethodInfo`), bases, extension methods, packed view regions, vtable slot order, layout, `[uuid]`, groups. |
| Template definitions | `ProgramModel` | Type templates by name (several per name, one per declaring scope), function templates by simple name (overloads, namespaces and extension templates side by side), out-of-line template method bodies keyed by (owner, method, arity) and matched by parameter types. |
| Instantiation caches | `ProgramModel` (mutable) | Value-keyed caches of instantiated types and function records, plus the bindings each was produced with. |
| Test registry | `ProgramModel` | `[test]`, `[test_setup]`, `[test_teardown]` metadata collected by the builder; read by the codegen harness. |

Ownership invariant: the store owns every type and the symbol table owns every record for the lifetime of
the compilation. A function-local type whose name has gone out of scope still exists in the store, so HIR
nodes and LSP caches can hold `Type*` / `FunctionRecord*` without lifetime reasoning.

### Function symbols

Every C4 function has its own emitted symbol, which is also its identity in reflection (`FunctionSymbol` and
`MethodSymbol` in `SymbolTable.h`):

- `<path>_<name>_<parameters>`. The path is the enclosing namespaces and, for a method, its type and the
  types around it (`Geo_Box_Get_0`); a function-local type or function adds the enclosing function's path.
- `<parameters>` is the arity, then each parameter type's `SymbolToken` (`Model/Types/QualifiedName.h`): the
  qualified name with `*` as `P`, `&` as `R`, an extent as `A<n>` and template arguments as `T...E`
  (`Add_2int32int32`, `Take_1const_int32R`).
- A const method ends in `_const` and an extension method names its receiver (`Bump_0_on_Point`). A
  file-scope `static` function or variable ends in `_u<unit>`.
- A template instance is named for its arguments, never for when it was made, so every program with the same
  code names it alike (a script and its host, [scripting](../runtime/04-scripting.md#41-names-that-must-agree)):
  `_T<arguments>_E`, the arguments' qualified names with every character that is not a letter or digit written
  `_xx` (`QualifiedName.h` `EscapedText`), so distinct argument lists never write alike. A class instance's
  scope is `Box_Tint32_E` (its members `Box_Tint32_E_Get_0`); a function-template instance is
  `Twice_Tint32_E_1int32`, plus `_on_<receiver>` for an extension template; a member type of an instance names
  the instance (`Node_IBox_3cint32_3e_E`).
- A lambda is named for the code that writes it and its place there: `__c4_closure_<function symbol>_<k>` (the
  k-th lambda of that function, or of that global's start). The census shows it as people read it
  (`Closure::displayName`): `lambda in Profile::StartSession`, `lambda #2 in ...` from the second on. A compiler-written lifecycle function is named for its
  operation and its type's scope: `__c4_delete_Holder_Tint32_E`.
- `SymbolTable::AddFunctionRecord` appends `_<n>` to a symbol another function already has, so no two
  functions share one. A builtin or `[link]` symbol names a foreign function and is used as written.

### Object layout

`ObjectType::CalculateLayout` is the single layout authority (codegen transcribes it, never re-packs):

- the MSVC order: polymorphic bases first (the first is the primary base and shares the type's vptr at
  offset 0), the type's own vptr at offset 0 when it is polymorphic without a polymorphic base, then the
  other bases, then the non-static fields at natural alignment; a base keeps its whole padded size
  (`GetBaseOffset(i)`, `GetPrimaryBase`, `HasOwnVptr`, `IsPolymorphic`);
- an anonymous union is one region at one offset; its fields flatten into the owner's member namespace;
- a `packed` lane view becomes a `PackedViewRegion` holding one SIMD `<N x T>`; aliased views of the same
  width and element share lanes; a type whose only storage is one packed region is a **pure packed
  vector** (`IsPurePackedVector`), sized exactly `N * sizeof(T)` and lowered as the vector itself;
- statics are not part of the instance; layout is computed lazily and memoized, so `sizeof(T)` is correct at
  any point once `T`'s fields are known (even mid pass 2).

A type also records its identity for RTTI and exception matching: its kind (class / struct / interface,
from the declaration), the enclosing namespaces and classes, and a template instance's arguments
(`Model/Builder/ObjectIdentity`).

Every question about the hierarchy goes through two bricks (shared with HIR and codegen):

- `Model/Types/ClassHierarchy`: where a base sub-object sits (`LocateBase`, ambiguity included),
  `DerivesFrom`, `PointerDelta` (the byte delta between related object pointers), the sub-object walks
  (`ForEachSubobject`, `Subobjects`), `FindField` across bases, and the base-list rules (`CheckBaseList`:
  one class base written first, interfaces derive only from interfaces, no repeated or already-inherited
  base; `CheckInterfaceMembers`: no instance fields). The model reports rule violations after
  `FinalizeLayouts` (`ModelBuilder::CheckObjectTypes`) and when a template instance or local type is made.
- `Model/Types/VirtualTables`: virtual slots keyed by name plus parameter types (`PrimarySlots`: the
  primary base's slots in order, overrides in place, new virtuals appended), every vptr of a type
  (`VptrLocations`), the method a slot runs in a complete object (`FinalOverrider`), and how a call through
  a receiver dispatches (`DispatchFor`).

Member, operator and cast lookup and overload candidates visit every base sub-object; a name inherited
from two unrelated bases with one signature is ambiguous (`SymbolQueries::InheritedSignatureClash`).

## Build passes

`ModelBuilder::Build` (`Model/Builder/`) runs exactly two passes over every reachable unit's own top-level
declarations, then finalizes:

1. **Pass 1 - register.** Every declaration becomes a shell: object types exist (incomplete), names are
   registered in the scope tree, template definitions and out-of-line template methods are recorded,
   pending work lists are filled (variables, fields, packed views, method declarations, method definitions,
   free functions, qualified functions). Nothing is resolved, so declaration order never matters.
   `BeginResolution()` then marks the model queryable.
2. **Pass 2 - resolve to a fixed point.** `ModelResolver::ResolveRound` walks the work lists - variables,
   enums, aliases, unions, object members (fields, bases, in-class method declarations), and via
   `FreeFunctionResolver` free functions, extension methods and qualified out-of-line definitions - plus
   `ConstEvaluator::FoldGlobalConstants`, repeating until a round changes nothing. A declaration and its
   definition intern to one signature, so the body attaches to the existing record instead of adding an
   overload.
3. **Body references.** `ModelResolver::InstantiateBodyReferences` walks each function body once to resolve
   every template specialization it names, so those types are concrete before HIR runs. This is the only
   place the model opens a body.
4. **Layouts.** `TypeStore::FinalizeLayouts` computes size/alignment for every object type (memoized).

Build-time type resolution is program-wide (unit 0). Errors (duplicate definitions, unresolved types) are
reported through the builder's error tracker.

## Query API

Inputs are source-level (a `QualTypeNode` as written, a name as written, operand types, an operator
token); outputs are `QueryResult<T>`. The caller never sees a candidate list, a template definition or the
symbol table.

| Verb | Answers |
|---|---|
| `ResolveType(node, unitId)` | A type as written: builtin, alias (transparent - same instance), qualified path, template specialization (instantiated here), trailing indirections. |
| `ResolveTypeInUnitContext(node, unitId, scope)` | Same, at lowering time: include-closure visibility of `unitId` plus the function's enclosing namespace/class, so unqualified names declared there resolve. |
| `QueryType(name, unitId, scope)` | Name form (`"Math::Vec"`). A template name without arguments is `NotFound` with a hint. |
| `QueryFunction(name, receiver, tmplArgs, argTypes, ctx, outRecord)` | ONE verb for free functions, methods, operators-by-name (`operator_add`), and constructors. Overloads resolve here; function templates instantiate here. `receiver` non-null means "must exist on this type". `outRecord` names the specific declaration. |
| `QueryOperator(token, lhs, rhs, ctx)` | `a + b`: token or canonical name, null `rhs` = unary. Looks for a method on `lhs` first, then `rhs`; for compound assignment prefers `<op>_assign`, falls back to the plain operator. Returns `OperatorBinding` (method, `rightIsReceiver`, `isAssignForm`). |
| `QueryVariable(name, receiver, ctx)` | ONE verb for local / member / global / static. Unqualified inside a method: member of the enclosing type, then statics of enclosing types, then globals. Constants come back with their folded value. |
| `QueryMember(type, name)` | Field or method by name, most-derived first (own, bases, extension methods). |
| `QuerySymbol`, `QueryConstant` | Raw symbol lookup; folded constant by qualified name (`Color::Green`). Cycles report `CyclicDependency`. |
| `SizeOf`, `AlignOf`, `ElementOf` | Layout facts. A forward-declared (incomplete) type is `Ok` with size 0. |
| `QueryInstantiation(node)` | For callers holding a specialization node directly (tests, LSP). |
| `RecordFor(sig)`, `RecordForMethod(type, method)` | The lowering record behind a resolved signature or method. |
| `TypeScope(obj)` | The class scope that holds a type's members. |
| `TypeInstantiationBindings`, `FunctionInstantiationBindings` | The bindings an instantiation was produced with (used to push fake aliases when lowering its bodies). |
| `AllFunctionRecords`, `ObjectTypes`, `EnumTypes` | Enumeration for codegen, reflection and the LSP. |

`LookupContext { unitId, enclosingType, scope, receiver, operand }` is the only "who is asking" context a
query needs: `unitId` selects the visibility domain, `enclosingType` makes unqualified member names resolve,
`scope` is where unqualified lookup starts (the function's namespace or class). `receiver` / `operand`
(`ReceiverAccess::Writable` or `ReadOnly`) say how the object of a method call, or each operand of an
operator, may be used (D-31): `SymbolQueries::SelectByReceiverAccess` keeps only const methods for a
read-only receiver (`QueryError::ReadOnly` when none is left: `'Set' is not a const method, so it cannot be
called on a const object`), and passes over a const method whose non-const twin with the same parameters is
also a candidate for a writable one.

`QueryError` kinds: `NotFound`, `NotVisible`, `Ambiguous`, `NotCallable`, `NotAValue`, `CyclicDependency`,
`Unsupported`, `ReadOnly`. Every failure carries a message written for the user, and the location where it was found.

`TypeHandle` (`Model/TypeHandle.h`) wraps a `const Type*` with capability queries (`IsNumeric`,
`IsPointerLike`, `Pointee`, `DerefAll`, `ResolveAlias`, size/alignment) so consumers do not hand-roll
pointer/reference/alias unwrapping. It is the one home for indirection reasoning.

Internals behind the verbs: `TypeResolver` (every type position), `SymbolQueries` (every name query),
`TemplateInstantiator` and `Model/Templates/` (below), `OverloadResolver`, `LoweringScopes`,
`ConstEvaluator`. Each is friended into `ProgramModel`; the public surface stays the verbs above.

## Const layers (D-30)

`TypeResolver::ApplyIndirections` walks the layers inside out keeping the constness of the current value:
the leading `const` makes the base const, each pointer or reference built over a const value points at const
(`PointerType::IsConstReferent`), a `const` before a `*` makes that pointer the const value for the next
layer, and an array of const elements stays const. The constness left after the outermost layer is the
declared entity's own (`QualTypeNode::DeclaresConstValue`): `SymbolInfo::isConst` for a variable,
`FieldInfo::m_IsConst`, `FunctionRecord::paramIsConst`. Types carry no top-level const, so a template
argument, a return type or a cast target ignores it. The parser rejects a `const` with no layer after it and
a const reference layer.

## Overload resolution

`OverloadResolver::Resolve` (`Model/OverloadResolution.cpp`) is the one ranking mechanism for free
functions, methods, operators and constructors:

1. Filter by arity: at least `minArity` (parameters minus trailing defaults), at most the parameter count
   unless variadic.
2. Score each argument with `ParamCost`; the candidate cost is the sum. Lowest wins; between equal costs a
   function written as it is beats a function-template instance (D-17); any other tie is `Ambiguous`
   ("equally good overloads: A and B"); no viable candidate is "no matching overload of F".
3. Trailing defaults are filled at the call site from `FunctionRecord::paramDefaults`, re-lowered in the
   caller's context per call.

| Argument -> parameter | Cost |
|---|---|
| Exact (after alias unwrap) | 0 |
| Braced list `{...}` -> `__init_list<T>` or `__init_list<T>*` | 0 (not viable for anything else) |
| Reference parameter | exact referent only (no conversion through references) |
| Integer -> enum parameter of that underlying type | 1 |
| Numeric -> numeric, same category (int->int, float->float) | 1 |
| Numeric -> numeric, across categories | 2 |
| Any pointer <-> `void*` (recursively, so `T**` <-> `void**`) | 1 |
| `T*` -> `const T*` (adding const; dropping const is not viable) | 1 |
| `Derived*` -> `Base*` (any base) | 1 |
| Array `T[N]` -> `T*`, `const T*` or `void*` (decay) | 1 |
| Packed swizzle run -> by-value pure packed vector of same width/element | 1 |
| Argument passed through `...` | 1 each (an exact non-variadic overload beats a variadic one) |

Numeric narrowing is implicit and unwarned. Whether that should stay is an **Open question** (see
[../status/roadmap.md](../status/roadmap.md)).

## Templates

Templates are instantiated **implicitly, lazily and cached** - a caller asks for `Vec<int32>` and gets an
ordinary concrete `ObjectType`, indistinguishable from a hand-written class.

- Kinds: class/struct/interface templates, function templates, templated extension methods, alias
  templates; type parameters (`T`, `class T`, `typename T`) and value parameters (`int32 N`), both with
  optional defaults.
- **Arguments are an ordered list** (`Model/Templates/TemplateArguments`: `TemplateArg` is a type or a
  `ConstantValue`). `TemplateArgumentResolver::Explicit` resolves what a use writes against the parameter
  kinds by position (a value argument is any constant expression, or a name the parser read as a type);
  `CompleteAndBind` fills the rest from deduction or defaults and binds each parameter in the instance's
  binding layer as it goes, so a default may name an earlier one.
- **Deduction** (`Model/Templates/TemplateDeduction`) matches each function parameter's type as written
  against its argument type (layers outermost first: `&`, `*`, `[N]`; then the base name: a parameter,
  a template instance - or a base of the argument that is one - or a `funcptr<R(A)>`). Written arguments
  are never deduced; two different answers are an error.
- **Function templates are overload-set members** (`Model/Templates/FunctionTemplates`): `QueryFunction`
  collects every visible template of the name (qualified names look in their namespace; member calls look
  at extension templates), deduces, completes and instantiates each into a candidate, and the overload
  resolver ranks them with the ordinary functions. A template declared ahead of its definition is one
  function with it: the declaration's instance is replaced by the definition's for the same arguments when
  their signatures agree. `AttachExtensions` gives every new object-template
  instance the extension templates whose receiver it is, as extension methods.
- **Object instances** are built by `Model/Templates/ObjectTemplateBody` (bases, aliases, fields, packed
  regions, methods). A method declared without a body takes the out-of-line definition with the same name
  and parameter types; a definition that renames the template parameters gets its own bindings.
- **Each instance has a class layer** (`TemplateInstantiator::InstanceScope`, named for its arguments beside the
  template, `Box_Tint32_E`, recorded in `TypeScope`). Its static data members are symbols there (`ObjectTemplateBody::AddStatic`:
  one object per instance, folded in the instance's bindings when constant; the program-wide constant sweep
  never sees their initializers, which stay on the declarator), and every member's emitted symbol is named
  from it, so two instances never share a symbol (BUG-115, BUG-116).
- **Member function templates** (`Model/Templates/MemberTemplates`, BUG-117) are `TemplateDef`s of their
  owner type (`memberOf`), kept per type in `m_MemberTemplates` (a deque: definitions are referenced by
  address). A plain class's are declared in pass 1 and their one-header out-of-line definitions attached
  after it; a class template instance's are declared while its body is built, with `bindingParent` = the
  instance's binding layer, and their two-header definitions (`template<T> template<U> R S<T>::F(U)`)
  attached with the names the definition gives the class's parameters (`ownerNames`). `QueryFunction`
  collects them for a receiver, for the enclosing type of an unqualified call, and for a qualifier that
  names a type (`P::F`); their instances are methods of the owner (`containingType`, not extensions).
- **Packs** (D-19): a pack parameter binds a `SymbolInfo` whose `packElements` are what the name means in
  each element (`TemplateArgumentResolver::PackSymbol`); `TemplateArgs` stay flat (fixed parameters, then
  the pack's elements). `Model/Templates/Packs` finds the packs a pattern names (`In`, `Names`, `Find`) and
  expands a template argument list (`ExpandArguments`: the pattern resolved once per element in a scope where
  each pack means that element). Deduction gives a trailing function parameter pack the remaining arguments
  and matches `Tuple<Ts...>` against an instance's trailing arguments element by element. A function
  instance records its parameter packs (`FunctionRecord::parameterPacks`: `args` is the parameters
  `args#0..`), which lowering binds as a pack of those parameters. `ConstEvaluator` folds `sizeof...` and
  fold expressions over constants.
- **Specializations** (D-20, `Model/Templates/Specializations`) live beside their primary
  (`m_TypeSpecializations`, `m_FunctionSpecializations`). An instance's arguments are matched against each
  pattern by deduction and then by re-resolving the pattern in the bindings it found (a trailing expansion
  takes the rest); the most specialized match wins by `Model/Templates/PartialOrdering`, which matches
  patterns structurally (`TemplatePatterns`: the fixed patterns and a trailing expansion; a top-level const
  is ignored, as it is not part of a type). An explicit function specialization replaces the instance's
  body when its signature is the primary's; overload resolution ranks equally good template instances by
  the same partial ordering (`TemplateOrdering`).
- **A template's name as a value** (D-17): where a funcptr or func is expected, `QueryTemplateName` gives a
  `TemplateNameType` that finds the instance a target signature deduces (`FunctionTemplates::ForTarget`:
  the target's parameter types as arguments and its return type as a hint, then an exact signature match).
  Overload resolution asks it whether a funcptr/func parameter fits (cost 0, a func 1); HIR binds it to the
  chosen parameter's signature.
- **Bindings are fake aliases.** Instantiation (and later HIR lowering of an instantiated body) pushes a
  scope layer in which `T` is registered as an alias of the bound type and `N` as a constant. Ordinary name
  lookup then finds them. The AST is never rewritten and no substitution engine exists (a substitution
  engine was considered and rejected: it duplicates the resolver and drifts).
- **Caching is by value.** The key is the chosen definition plus the resolved argument types and folded
  non-type values, so `alias A = TBox<float>; alias B = TBox<float>;` yield one instance, and same-named
  templates in different namespaces stay distinct. The innermost declaring scope on the lookup chain wins.
- Instantiating a class template creates records for all of its methods (their signatures are needed for
  lookup and vtables), each marked `TemplateOrigin::ClassMember`; codegen lowers one only when emitted code
  references it (D-16, [compiler/04](04-codegen-and-abi.md)).
- Recursion safety: an instantiation is registered before its members resolve, so self-referential
  templates (`Node<T>` holding `Node<T>*`) terminate.

Status lives in [../status/implementation-status.md](../status/implementation-status.md).

## Return lists

A return list type `[T1, T2]` (D-34, `QualTypeNode::IsReturnList`) resolves in `TypeResolver::ResolveReturnList`:
each element in the asking bindings (a `Ts...` element once per pack element), then `Model/Types/ReturnLists::Of`,
which makes one anonymous struct per list of element types (fields `_0`, `_1`, ...; `ObjectType::IsReturnList`),
lays it out like a C struct, registers it with the program's object types and keeps it in `m_ReturnLists`. It is
an ordinary struct to every later phase (the ABI returns it like one); reflection skips it.

## Lowering-time scopes

While HIR lowers a body it changes scopes ONLY through the model's RAII API (`Model/LoweringScopes`):

- `PushScope(kind, name)` returns a `ScopeGuard` that pops its layer on destruction, including on early
  return after an error, so a leak into the next function is structurally impossible. The layer is pushed on the
  calling thread's `ScopeCursor` (one per `LowerFunction`, so lowerings on several threads keep their layers
  apart), else on the table's own position.
- `RegisterLocal(name, info)` adds a local to the current layer; every query sees it until the layer pops
  (innermost wins: shadowing falls out of the lookup order).
- `RegisterLocalType(decl)`, `RegisterLocalAlias(alias)`, `RegisterLocalFunction(fn)`: a struct/class/enum,
  alias or function declared inside a body becomes a concrete type/record in the store, with its name only
  in the current layer. It is visible exactly where declared and disappears when the block ends.
- `FindLocal(name)` searches the lowering chain only, and treats each Function layer as a frame boundary: a
  body lowered inside another one's lowering (a lambda's call method, a local function) still sees the
  enclosing layers' types, aliases and template bindings, but not their variables (`SymbolInfo::IsFrameVariable`;
  `QueryVariable` never returns one from the global path either). A lambda reaches them through captures.
- `CreateClosure(spec)` / `ClosureOf(type)` (`Model/Closures`): a lambda's closure type - a struct whose
  fields are the captures (`ClosureCapture`: by value, by reference as a pointer, `this` as a pointer,
  `*this` as a copy) with an `[operator("call")]` method record (static when nothing is captured), built
  while the enclosing function lowers. Closures are not reflected.

## Changes and the frozen model

The model changes after it is built only while bodies are lowered (instances, compiler-written lifecycle
functions, closures, local types, lazily kept answers), and lowering runs on several threads
([03-hir](03-hir.md#lowering-on-several-threads)). So every change asks the model's `ModelWrites` first
(`ProgramModel::Writes()`): `TypeStore::Own`, `SymbolTable::AddFunctionRecord`, `CreateScope` of a class or
namespace scope, `Scope::Register` on a permanent scope, `ObjectType::AddField` / `AddMethod` / `AddBaseType` /
`CalculateLayout`, `EnsureSynthesizedLifecycle`, `LifecycleMethodsOf`'s memo, `StorePack`, `CreateClosure`, the
template instantiators' misses. A frozen model answers with `ModelFrozen` instead; a template instance's miss
carries how to make it (`ModelFrozen::make`). `ProgramModel::Settle()` makes every lazily kept answer lowering
reads, so a frozen model is never asked for one. Interning is not a change: the type store's caches are read
under a shared lock and filled under their own.

## Constant evaluation

`ConstEvaluator` (`Model/ConstEvaluator.h`) folds compile-time expressions: integer/float/bool literals,
arithmetic, bitwise, logical (`&&`, `||`) and comparison operators, unary `+ - !`, parentheses, `sizeof`
(resolved in a template's binding layer), `sizeof...`, fold expressions, and references to other constants
(const variables, enum members, template non-type parameters) resolved through the model.

- **On demand:** `Evaluate(expr, unitId, bindingScope)` folds any expression when asked (array sizes, enum
  values, global initializers, annotation arguments). `bindingScope` lets a template's `N` fold to its bound
  value.
- **Order-independent:** `FoldGlobalConstants` is one sweep of the pass-2 fixed point; forward references,
  chains and diamonds settle across rounds. A const that names itself reports `CyclicDependency` instead of
  hanging.
- Folded constants have no storage: a `const` global or local becomes its value.

**Planned:** compile-time evaluation of user functions (constexpr-style) is not implemented.

## OperatorTable

`Model/OperatorTable` is the single operator catalog. `AnnotationFor(token, unary)` maps a source token to
its canonical name for an arity (`*` -> `multiply` or `dereference`, `&` -> `bitwise_and` or `address_of`,
`-` -> `subtract` or `negate`); `CanonicalAnnotation` normalizes whatever was written in `[operator("...")]`
(`"[]"` -> `index`, `"+"` -> `add`); `IsAssignForm` / `PlainFormFor` drive the compound-assignment fallback
(`and_assign` -> `bitwise_and`). Names with no token (`create`, `delete`, `copy`, `move`, `cast`, `begin`,
`end`, custom names like `hash`) pass through unchanged.

`Model/OperatorAnnotations` collects every `[operator(...)]` annotation on a declaration (several names per
annotation and chained annotations both allowed) into `FunctionRecord::operatorAnnotations`. Both
`QueryOperator` (annotation -> candidates) and `QueryFunction("operator_<name>")` read that set, which is why
`v.operator_hash()` needs no special path. The user-facing table is in
[../language/06-lifecycle-and-operators.md](../language/06-lifecycle-and-operators.md).

Rule enforced by the builder: an operator must be a member or an `extends` extension method; a free
`[operator]` function is not an operator.

## Builtins

The runtime's C entry points (CRT memory/string/math, `printf`, GC, byte helpers, file I/O, reflection, test
assertions) and `_GUID` are C4 declarations in the prelude, `Bedrock/Prelude/prelude.ch` (D-43). The source manager
registers it with the default library (`PreludeId()`); `ModelBuilder::VisitPrelude` visits it after every other
unit and skips a declaration whose name the program already declared at file scope, so the program's wins; each
unit's include closure gets the prelude's unit (`ProgramModel::PreludeUnit()`), and its free functions are marked
`isBuiltin` (not reflected). `sizeof`, `__uuidof`, `__init_list<T>`, `take` / `destroy` / `relocate` and the
`va_*` forms stay compiler intrinsics. The list is in [../language/14-builtins.md](../language/14-builtins.md).
