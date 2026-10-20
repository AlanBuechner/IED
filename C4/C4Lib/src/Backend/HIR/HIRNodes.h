#pragma once
#include "Backend/HIR/HIRValue.h"
#include "Model/SymbolTable.h" // FunctionRecord
#include <memory>
#include <deque>

namespace Scripting
{
	// =====================================================================
	// HIR NODE TAXONOMY (03.5) - the closed set. Every node carries a concrete
	// Type*. There are no identifier nodes, no operator-token nodes, and no
	// "unresolved" nodes in a finished tree: every name is bound to a slot/field/
	// global, every operator is already a Call or an IntrinsicOp. LValues (Slot /
	// Field / Global / Deref) are descriptors in HIRValue.h, NOT nodes - codegen
	// turns them into load/store pairs; HIR only names the location.
	// =====================================================================
	struct HIRNode
	{
		enum class Kind
		{
			// rvalue-producing (a quantity)
			Constant, Load, Call, IntrinsicOp, Convert, New, Delete, Construct, AddressOf, AddressArithmetic, Ternary, InitList, FunctionAddress, VectorBuild, Sequence, DynamicCast, Relocate, MakeFunc, FuncCode, Transfer, Va, Atomic, Simd,
			// statements / regions
			Block, If, While, For, Switch, Break, Continue, Jump, Goto, Label, Return, ExprStmt, LocalDecl, Store, TryCatch, Throw, Defer, Once, AtExit
		};

		Kind kind;
		const Type* type = nullptr; // ALWAYS concrete (HIR contract)
		SourceLocation origin{};    // where in the source this came from (diagnostics + debug)
		HIRNode* parent = nullptr;  // owning node (region/parent); break/continue walk this up

		explicit HIRNode(Kind k, const Type* t = nullptr) : kind(k), type(t) {}
		virtual ~HIRNode() = default;
	};

	// A built-in operator on PRIMITIVES (the honest counterpart to Call: a primitive
	// `a + b` is an operation, not a method call). OpKind mirrors the model's operator
	// vocabulary (OperatorTable) so HIR and the model cannot disagree about what `+` means.
	enum class IntrinsicOp
	{
		Add, Sub, Mul, Div, Mod, Pow,
		Eq, Ne, Lt, Gt, Le, Ge,
		BitAnd, BitOr, BitXor, Shl, Shr,
		Negate, BitNot, BoolNot // unary (Negate = numeric -, BitNot = ~, BoolNot = !)
	};

	// ---------------------------------------------------------------------
	// rvalue-producing nodes
	// ---------------------------------------------------------------------
	struct ConstantNode : HIRNode
	{
		ConstantValue value;
		ConstantNode(const Type* t, ConstantValue v) : HIRNode(Kind::Constant, t), value(v) {}
	};

	// Read an lvalue into an rvalue. Emitted by AsRValue whenever an lvalue must be a quantity.
	struct LoadNode : HIRNode
	{
		const LValue* loc; // the location being read (element type == this->type)
		LoadNode(const Type* t, const LValue* l) : HIRNode(Kind::Load, t), loc(l) {}
	};

	// A resolved call: a free function, a method (receiver set), or an operator resolved to a
	// method. receiver null = free / static; non-null = the `this` lvalue for a method call.
	struct CallNode : HIRNode
	{
		const FunctionRecord* callee = nullptr; // the lowering record (mangled symbol, body); null for indirect
		const LValue* receiver = nullptr;       // implicit this (method), or null
		const HIRNode* target = nullptr;        // INDIRECT call: the function-pointer rvalue to call through
		bool isOperatorCall = false;            // true when this came from `a + b` etc.
		bool materialize = false;               // member-access on a by-value aggregate result: yield the temp ADDRESS, not the value
		int32_t virtualIndex = -1;              // >=0 when this is a VIRTUAL dispatch: the vtable slot to call through (receiver's vptr)
		std::vector<const HIRNode*> args;       // rvalues, in source order (this NOT included)
		CallNode(const Type* t, const FunctionRecord* c) : HIRNode(Kind::Call, t), callee(c) {}
	};

	// A checked conversion of a pointer to a polymorphic object (`p as T*`, `dynamic_cast<T*>(p)`, `p is T`):
	// the runtime reads the object's type information and yields the `target` sub-object, or null when the
	// dynamic type is not a `target`. testOnly: the result is the bool `!= null` (`is`).
	struct DynamicCastNode : HIRNode
	{
		const HIRNode* operand = nullptr;    // pointer rvalue to a polymorphic object
		const ObjectType* target = nullptr;
		bool testOnly = false;
		DynamicCastNode(const Type* t, const HIRNode* op, const ObjectType* tg, bool test)
			: HIRNode(Kind::DynamicCast, t), operand(op), target(tg), testOnly(test) {}
	};

	// The address of a named function used as a value (function-to-pointer decay): `f = Add`,
	// `Apply(Add, 1, 2)`. Result is a funcptr rvalue of the function's exact signature; codegen
	// emits the symbol's address.
	struct FunctionAddressNode : HIRNode
	{
		const FunctionRecord* fn = nullptr; // the record (mangled symbol to load the address from)
		FunctionAddressNode(const Type* fpType, const FunctionRecord* f) : HIRNode(Kind::FunctionAddress, fpType), fn(f) {}
	};

	// A func<R(A)> value (docs/language/07): its code and environment. From a funcptr (`source`, called through
	// the signature's thunk) or null; from a closure object (`source`, consumed: copied or relocated before)
	// whose call method is `code` - kept in a GC copy, or for a closure that is one pointer, that pointer.
	struct MakeFuncNode : HIRNode
	{
		enum class From { Null, Pointer, HeapClosure, PointerClosure };
		From from = From::Null;
		const HIRNode* source = nullptr;
		const FunctionRecord* code = nullptr;      // closures: the call method
		const FunctionRecord* finalizer = nullptr; // HeapClosure: the closure type's delete, when it has one
		MakeFuncNode(const Type* funcType, From f, const HIRNode* src) : HIRNode(Kind::MakeFunc, funcType), from(f), source(src) {}
	};

	// The code pointer of a func value, as a void* (a func is null when its code is).
	struct FuncCodeNode : HIRNode
	{
		const HIRNode* func = nullptr;
		FuncCodeNode(const Type* voidPtr, const HIRNode* f) : HIRNode(Kind::FuncCode, voidPtr), func(f) {}
	};

	// A built-in operator on primitives. rhs null = unary (Negate / BitNot).
	struct IntrinsicOpNode : HIRNode
	{
		IntrinsicOp op;
		const HIRNode* lhs = nullptr;
		const HIRNode* rhs = nullptr;
		IntrinsicOpNode(const Type* t, IntrinsicOp o, const HIRNode* l, const HIRNode* r)
			: HIRNode(Kind::IntrinsicOp, t), op(o), lhs(l), rhs(r) {
		}
	};

	// An explicit cast or implicit coercion. from/to are concrete.
	struct ConvertNode : HIRNode
	{
		const HIRNode* src = nullptr;
		const Type* from = nullptr;
		const Type* to = nullptr;
		bool keepsBits = false; // reinterpret_cast between packed vectors of one size (D-62): the same bits, not the lanes' values
		ConvertNode(const HIRNode* s, const Type* f, const Type* t)
			: HIRNode(Kind::Convert, t), src(s), from(f), to(t) {
		}
	};

	// Build a SIMD vector <K x T> from K scalar rvalues (a braced initializer assigned to a swizzle
	// run: `v.xz = { 1.0f, 2.0f }`). this->type is the packed sub-vector; elems are the lane values in
	// order. Codegen inserts each into a fresh vector register.
	struct VectorBuildNode : HIRNode
	{
		std::vector<const HIRNode*> elems; // K scalar rvalues, lane order
		VectorBuildNode(const Type* t) : HIRNode(Kind::VectorBuild, t) {}
	};

	// A sequence of expression-steps evaluated in order (side effects), yielding the LAST step's value.
	// One rvalue node for "do several things, then produce a result" - e.g. an unpacked swizzle write
	// `uv.rb = {5, 6}` stores each element into its own field; the stores must all run and in order. A step
	// may be a Block: its statements run in place (a loop copying or constructing an array's elements).
	struct SequenceNode : HIRNode
	{
		std::vector<const HIRNode*> steps; // evaluated left-to-right; the result is steps.back()
		bool fresh = false;                // the result is a new object the consumer owns (a copy, take(x))
		SequenceNode(const Type* t) : HIRNode(Kind::Sequence, t) {}
	};

	struct BlockNode;

	// Allocation. Result is a POINTER rvalue (the object's address); *p / p->x lower via Deref.
	struct NewNode : HIRNode
	{
		const Type* objectType = nullptr; // the type being allocated (pointee of this->type)
		std::vector<const HIRNode*> args;  // constructor args, source order
		const FunctionRecord* ctor = nullptr;   // [operator("create")] method, or null (aggregate / zero-init)
		const FunctionRecord* finalizer = nullptr; // [operator("delete")] method registered for GC reclamation, or null
		bool aggregateInit = false;        // new T{...}: args positionally fill the fields (no ctor)
		const HIRNode* placement = nullptr; // `new (p) T(...)`: the storage (a T*); null allocates on the managed heap
		// `new T[n]` (docs/language/10): `count` (an int64 rvalue) zeroed elements of objectType, each with its vptrs.
		// The pointer to the first is stored in `elements`, then `initialize` starts them; a heap array then gets a
		// finalizer destroying every element (`finalizer` is the element's, or its innermost element's, delete).
		const HIRNode* count = nullptr;
		const LValue* elements = nullptr;
		const BlockNode* initialize = nullptr;
		NewNode(const Type* ptrType, const Type* objType) : HIRNode(Kind::New, ptrType), objectType(objType) {}
	};

	// `relocate(dst, src)`: the object at `src` moves to the storage at `dst` (its bytes are copied, nothing
	// is constructed or destroyed) and `src` is left zero. Both operands are T* rvalues; the result is void.
	// `va_start(ap)`, `va_end(ap)`, `va_copy(dst, src)`, `va_arg(ap, T)` (D-35). `list` is the address of the
	// va_list variable, `source` the address of the list va_copy copies; a va_arg node's type is the T it reads.
	struct VaNode : HIRNode
	{
		enum class Op { Start, End, Copy, Arg };
		Op op = Op::Start;
		const HIRNode* list = nullptr;
		const HIRNode* source = nullptr;
		VaNode(const Type* t, Op o, const HIRNode* l, const HIRNode* s = nullptr) : HIRNode(Kind::Va, t), op(o), list(l), source(s) {}
	};

	// An atomic access through `address` (D-60): a load, store, exchange, compare-exchange or read-modify-write of a
	// `valueType`, or a fence (no address).
	struct AtomicNode : HIRNode
	{
		enum class Op { Load, Store, Exchange, CompareExchange, CompareExchangeWeak, FetchAdd, FetchSub, FetchAnd, FetchOr, FetchXor, Fence };
		enum class Order { Relaxed, Acquire, Release, AcqRel, SeqCst }; // MemoryOrder's members, in order
		Op op;
		Order order = Order::SeqCst;
		Order failureOrder = Order::SeqCst;    // a compare-exchange that finds another value
		const HIRNode* orderValue = nullptr;   // the order when only the run time knows it (else `order`)
		const HIRNode* failureOrderValue = nullptr;
		const Type* valueType = nullptr;       // what `address` points at
		const HIRNode* address = nullptr;
		const HIRNode* value = nullptr;        // stored, exchanged or combined; a compare-exchange's desired value
		const HIRNode* expected = nullptr;     // a compare-exchange's pointer to the value it expects
		AtomicNode(const Type* t, Op o) : HIRNode(Kind::Atomic, t), op(o) {}
	};

	// A SIMD intrinsic (D-62) on packed vectors: `operands` are rvalues in the intrinsic's order; `indices` are a
	// shuffle's lanes (index i of the result takes lane indices[i] of the operands, the second operand's after the
	// first's). A Splat broadcasts its one scalar operand into this->type.
	struct SimdNode : HIRNode
	{
		enum class Op
		{
			Min, Max, Abs, Sqrt, Floor, Ceil, Round, Trunc, MulAdd, Fma,
			Select, Any, All, Bitmask,
			Shuffle, Concat, Splat,
			ReduceAdd, ReduceMul, ReduceMin, ReduceMax, ReduceAnd, ReduceOr, ReduceXor,
			Load, Store, LoadMasked, StoreMasked, Gather, GatherMasked
		};
		Op op;
		std::vector<const HIRNode*> operands;
		std::vector<int32> indices;
		SimdNode(const Type* t, Op o) : HIRNode(Kind::Simd, t), op(o) {}
	};

	struct RelocateNode : HIRNode
	{
		const Type* objectType = nullptr;
		const HIRNode* destination = nullptr;
		const HIRNode* source = nullptr;
		RelocateNode(const Type* objType, const HIRNode* dst, const HIRNode* src)
			: HIRNode(Kind::Relocate, nullptr), objectType(objType), destination(dst), source(src) {}
	};

	// By-value construction: `T(args)` or `T{...}` as an EXPRESSION (the C++-style constructor call).
	// Unlike New (which returns a pointer to heap storage), this materializes the object in a local
	// temporary and yields it BY VALUE - so `OpVec2 v = OpVec2(1, 2);` stores the struct into the slot.
	// Codegen: alloca(obj) + zero-init, then run [operator("create")] with this=ptr OR positionally fill
	// the fields (aggregate), then load the object back as the rvalue.
	struct ConstructNode : HIRNode
	{
		const Type* objectType = nullptr; // the type being constructed (== this->type)
		std::vector<const HIRNode*> args;  // constructor / aggregate args, source order
		const FunctionRecord* ctor = nullptr;   // [operator("create")] method, or null (aggregate / zero-init)
		bool aggregateInit = false;        // T{...}: args positionally fill the fields (no ctor)
		ConstructNode(const Type* objType) : HIRNode(Kind::Construct, objType), objectType(objType) {}
	};

	// Deallocation: the symmetric counterpart of New. Operand is a POINTER rvalue (the object's
	// address). Result is void. Codegen expands this to an optional [operator("delete")] call on the
	// pointee type followed by the free - HIR just records the node, mirroring how New defers its
	// constructor/finalizer to codegen.
	struct DeleteNode : HIRNode
	{
		const Type* objectType = nullptr; // the pointee type being freed (nullptr if not a pointer)
		const HIRNode* operand = nullptr; // the pointer rvalue
		DeleteNode(const Type* objType, const HIRNode* op) : HIRNode(Kind::Delete, nullptr), objectType(objType), operand(op) {}
	};

	// `&x` - an lvalue becomes a pointer rvalue.
	struct AddressOfNode : HIRNode
	{
		const LValue* loc; // the location whose address is taken (type == pointee of this->type)
		bool transient = false; // the address lives only while its expression runs (a reference argument): it does not escape
		AddressOfNode(const Type* ptrType, const LValue* l) : HIRNode(Kind::AddressOf, ptrType), loc(l) {}
	};

	constexpr uint32 kNoDropFlag = 0xFFFFFFFFu;

	// A local's or by-value parameter's object handed to whatever it initializes (D-05): a `copy`, or - when
	// this is the local's last use and its address never escaped (LastUseMoves) - the object itself, moved
	// (its bytes, the local is not destroyed: its drop flag is cleared). The result is a fresh object.
	struct TransferNode : HIRNode
	{
		const LValue* source = nullptr; // the local's slot
		const HIRNode* copy = nullptr;  // the copy, used unless it moves
		bool move = false;
		uint32 dropFlag = kNoDropFlag;  // the bool slot the local's cleanup tests; cleared by the move
		TransferNode(const Type* t, const LValue* s, const HIRNode* c) : HIRNode(Kind::Transfer, t), source(s), copy(c) {}
	};

	// Pointer arithmetic: `base + offset` where base is a pointer rvalue and offset an integral
	// rvalue (already multiplied by the element size by the lowerer). The result is a POINTER of
	// the same pointee - it is the address half of a built-in `a[i]`, which Derefs to the element.
	struct AddressArithmeticNode : HIRNode
	{
		const HIRNode* base = nullptr;   // pointer rvalue (array address or loaded pointer)
		const HIRNode* offset = nullptr; // integral rvalue, in BYTES
		AddressArithmeticNode(const Type* ptrType, const HIRNode* b, const HIRNode* o)
			: HIRNode(Kind::AddressArithmetic, ptrType), base(b), offset(o) {
		}
	};

	// `c ? a : b` - an expression that yields a value. Both arms materialize to the result type.
	struct TernaryNode : HIRNode
	{
		const HIRNode* cond = nullptr;
		const HIRNode* thenExpr = nullptr;
		const HIRNode* elseExpr = nullptr;
		TernaryNode(const Type* t, const HIRNode* c, const HIRNode* th, const HIRNode* el)
			: HIRNode(Kind::Ternary, t), cond(c), thenExpr(th), elseExpr(el) {
		}
	};

	// `a && b` / `a || b` lower to a TernaryNode (c ? <rhs materialized> : <true/false const>) so the
	// rhs is only evaluated when needed - short-circuiting falls out of the ternary's region structure.

	// ---------------------------------------------------------------------
	// statement / region nodes
	// ---------------------------------------------------------------------
	// A sequence of statements. Also the scope carrier: the lowerer pushes a scope while lowering
	// it and pops it after (RAII). The function root is a Block.
	struct BlockNode : HIRNode
	{
		std::vector<HIRNode*> stmts;
		explicit BlockNode(const Type* t = nullptr) : HIRNode(Kind::Block, t) {}
	};

	struct IfNode : HIRNode
	{
		const HIRNode* cond = nullptr;
		BlockNode* thenBlock = nullptr;
		BlockNode* elseBlock = nullptr; // null when no else
		IfNode(const HIRNode* c) : HIRNode(Kind::If, nullptr), cond(c) {}
	};

	struct WhileNode : HIRNode
	{
		const HIRNode* cond = nullptr;
		BlockNode* body = nullptr;
		uint32 breakLabel = 0;    // join point id (assigned by the Regions subsystem)
		uint32 continueLabel = 0;
		bool isDoWhile = false;   // true: run the body once BEFORE testing cond (do-while)
		WhileNode(const HIRNode* c) : HIRNode(Kind::While, nullptr), cond(c) {}
	};

	struct ForNode : HIRNode
	{
		BlockNode* initBlock = nullptr; // lowered in its own scope (for-init lifetime)
		const HIRNode* cond = nullptr;  // null = infinite
		const HIRNode* step = nullptr;  // null = no step
		BlockNode* body = nullptr;
		uint32 breakLabel = 0;
		uint32 continueLabel = 0;
		ForNode() : HIRNode(Kind::For, nullptr) {}
	};

	struct SwitchCase { ConstantValue value; BlockNode* block = nullptr; uint32 entryLabel = 0; };
	struct SwitchNode : HIRNode
	{
		const HIRNode* cond = nullptr;
		std::vector<SwitchCase> cases;
		BlockNode* defaultBlock = nullptr; // null when no default
		uint32 defaultEntry = 0;           // the default slot's entry label (a `fallthrough;` from the prior case jumps here)
		uint32 breakLabel = 0;             // a `break` inside a case exits the switch (not any loop)
		SwitchNode(const HIRNode* c) : HIRNode(Kind::Switch, nullptr), cond(c) {}
	};

	struct BreakNode : HIRNode
	{
		uint32 targetLabel = 0;
		explicit BreakNode(uint32 l) : HIRNode(Kind::Break, nullptr), targetLabel(l) {}
	};
	// An unconditional jump to a label (switch fallthrough: leave a case for the next one).
	struct JumpNode : HIRNode
	{
		uint32 targetLabel = 0;
		explicit JumpNode(uint32 l) : HIRNode(Kind::Jump, nullptr), targetLabel(l) {}
	};
	// A `goto label`. HIR has checked the jump rules: the label is in the goto's block or an enclosing one
	// and a forward jump crosses no declaration or defer, so codegen can always find the label's block on
	// its current block path. Code after a goto is laid out as unreachable.
	struct GotoNode : HIRNode
	{
		uint32 targetLabel = 0;
		explicit GotoNode(uint32 l) : HIRNode(Kind::Goto, nullptr), targetLabel(l) {}
	};
	// A `label:` marker: the jump target for one or more gotos. Emission moves the insert point to the
	// label's block so subsequent statements land there.
	struct LabelNode : HIRNode
	{
		uint32 labelId = 0;
		explicit LabelNode(uint32 l) : HIRNode(Kind::Label, nullptr), labelId(l) {}
	};
	struct ContinueNode : HIRNode
	{
		uint32 targetLabel = 0;
		explicit ContinueNode(uint32 l) : HIRNode(Kind::Continue, nullptr), targetLabel(l) {}
	};

	struct ReturnNode : HIRNode
	{
		const HIRNode* value = nullptr; // null for void
		ReturnNode(const HIRNode* v) : HIRNode(Kind::Return, v ? v->type : nullptr), value(v) {}
	};

	// A statement that is an expression kept for its side effects (a call, a store, ++). The value
	// is discarded but must still be evaluated in order.
	struct ExprStmtNode : HIRNode
	{
		const HIRNode* expr; // the rvalue (or a Store) whose side effect matters
		ExprStmtNode(const HIRNode* e) : HIRNode(Kind::ExprStmt, nullptr), expr(e) {}
	};

	// A local declaration: allocates slot `slot`, registers the name in scope, and stores `init`.
	struct LocalDeclNode : HIRNode
	{
		uint32 slot = 0;
		const HIRNode* init = nullptr; // null when default-initialized
		uint32 liveFlag = kNoDropFlag; // a moved-from local's drop flag, set when the declaration runs (D-05)
		Core::String m_Name;           // kept for debug/diagnostics; lookup uses the scope chain
		LocalDeclNode(uint32 s, const Type* t, const HIRNode* i) : HIRNode(Kind::LocalDecl, t), slot(s), init(i) {}
	};

	// A region that runs the first time control reaches it and never again in the program (a static local's
	// initialization), even when threads race to it: the others wait for it. An exception leaving `body`
	// abandons the attempt, and the next pass runs it again.
	struct OnceNode : HIRNode
	{
		BlockNode* body = nullptr;
		Core::String m_Name; // names the guard
		bool perThread = false; // a thread_local's start: once on each thread, behind a guard of its own
		OnceNode(BlockNode* b, const Core::String& name, bool eachThread)
			: HIRNode(Kind::Once, nullptr), body(b), m_Name(name), perThread(eachThread) {}
	};

	// Registers `object` (a global, a static member or a static local) to be destroyed when the program exits:
	// `count` elements of `stride` bytes, each by `destroy`, last first. Objects registered later end first. A
	// thread_local object is the calling thread's copy, destroyed when that thread ends.
	struct AtExitNode : HIRNode
	{
		const LValue* object = nullptr;
		const FunctionRecord* destroy = nullptr;
		uint64 count = 1;
		uint64 stride = 0;
		bool perThread = false;
		AtExitNode(const LValue* o, const FunctionRecord* d, uint64 n, uint64 size, bool eachThread)
			: HIRNode(Kind::AtExit, nullptr), object(o), destroy(d), count(n), stride(size), perThread(eachThread) {}
	};

	// Write an rvalue into an lvalue. Also the emission form of assignment / compound-assign result.
	struct StoreNode : HIRNode
	{
		const LValue* target; // where to write
		const HIRNode* value; // what to write (rvalue)
		StoreNode(const LValue* t, const HIRNode* v) : HIRNode(Kind::Store, v ? v->type : nullptr), target(t), value(v) {}
	};

	struct TryNode : HIRNode
	{
		BlockNode* tryBlock = nullptr;
		// One entry per catch clause, in source order. The FIRST clause whose type matches the
		// thrown value runs (dispatch/matching is a codegen concern); each names its caught-value slot.
		// A `finally` is not part of the node: HIR lowers it as a defer around the try statement. A clause
		// catches `type` by value, or by reference (its slot then holds the exception object's address), or
		// everything (`catch (...)`, type null).
		// type: the caught value type (a reference clause's referent). constQualified: `const T&` or `const T*`.
		struct CatchClause
		{
			const Type* type = nullptr;
			uint32 slot = 0;
			BlockNode* block = nullptr;
			bool byReference = false;
			bool constQualified = false;
			bool catchAll = false;
			SourceLocation origin{}; // the `catch`: where the handler's entry is in the debug information
		};
		std::vector<CatchClause> catches;
		TryNode() : HIRNode(Kind::TryCatch, nullptr) {}
	};

	struct ThrowNode : HIRNode
	{
		const HIRNode* value = nullptr; // null for a rethrow (bare `throw;`)
		bool isRethrow = false;
		// The lifecycle the EH tables run on the exception object: its destruction after the last handler,
		// and the copy into a by-value catch of the type or of each base (null where bytes suffice).
		const FunctionRecord* destroy = nullptr;
		std::vector<std::pair<const ObjectType*, const FunctionRecord*>> copies;
		explicit ThrowNode(const HIRNode* v, bool rethrow) : HIRNode(Kind::Throw, v ? v->type : nullptr), value(v), isRethrow(rethrow) {}
	};

	// A scope-exit cleanup (`defer`, a value local's destructor, a `finally`): `body` runs when control
	// leaves the enclosing block by any edge - falling off the end, return, break, continue, goto,
	// fallthrough, or an exception unwinding - LIFO with the block's other cleanups. A cleanup that has not
	// been reached when the block exits does not run. Codegen keeps them on its CleanupStack.
	struct DeferNode : HIRNode
	{
		BlockNode* body = nullptr; // the deferred statement(s): an expression-statement region or a block
		bool exceptionalOnly = false; // runs only when an exception unwinds the block (undoing a partial build)
		DeferNode() : HIRNode(Kind::Defer, nullptr) {}
	};

	struct InitListNode : HIRNode
	{
		// A braced initializer list `{e0, e1, ...}` used where a __init_list<T> is expected (a call
		// argument, or an aggregate init). The elements are materialized rvalues in source order; the
		// node's type is the target __init_list<T>. Codegen allocates a temporary array for the
		// elements and builds the 16-byte { const T* begin, size_type size } aggregate (the model's
		// std::initializer_list ABI). Empty `{}` yields zero elements.
		std::vector<const HIRNode*> elements;
		InitListNode(const Type* t) : HIRNode(Kind::InitList, t) {}
	};

	// =====================================================================
	// A lowered function: owns its arena (all nodes + lvalues), its slot table, and its root region.
	// All storage is freed together when the HIRFunction is destroyed - which is what makes lowering
	// a template body N times safe (each instantiation gets its own tree; nothing shared but the
	// read-only model and AST). See 03.7.
	// =====================================================================
	enum class SlotClass { Param, Local, Temp, Static };

	struct HIRFunction
	{
		const FunctionRecord* record = nullptr; // the function this tree is for
		BlockNode* root = nullptr;              // the top-level block (scope carrier)
		bool complete = false;                  // true when lowering finished without error

		// Slot table: parameters, locals, and temporaries in one indexed array. Codegen lays out
		// stack space from this single table. Order is allocation order (params first).
		// A static slot whose start is only a constant store starts holding `initial` instead (no guard).
		struct Slot
		{
			const Type* type = nullptr;
			Core::String name;
			SlotClass cls = SlotClass::Local;
			const ConstantNode* initial = nullptr;
			bool isVolatile = false;    // every load and store of it is volatile (D-29)
			bool isThreadLocal = false; // a static slot each thread has its own copy of (D-29)
			SourceLocation declared;    // the statement that made it (debug information)
		};
		std::vector<Slot> slots;

		// Arena: every node and lvalue lives here and dies with the function. deque gives stable
		// addresses on push (LValue pointers are referenced by nodes and other lvalues).
		std::deque<LValue> m_LValues;
		std::vector<std::unique_ptr<HIRNode>> m_Nodes;

		// Functions declared INSIDE this body (local functions, doc 06): each is a fully lowered
		// HIRFunction owned here so codegen can emit it alongside its parent.
		std::vector<std::unique_ptr<HIRFunction>> nestedFunctions;

		// The source location of the statement being lowered: every node and slot made meanwhile carries it (B28).
		SourceLocation location;

		// --- allocation helpers (the ONLY way to create HIR storage) -------------------------
		LValue* NewLValue(const LValue& v) { m_LValues.push_back(v); return &m_LValues.back(); }
		uint32 AddSlot(const Type* t, const Core::String& name, SlotClass c)
		{
			Slot slot{ t, name, c };
			slot.declared = location;
			slots.push_back(slot);
			return static_cast<uint32>(slots.size()) - 1;
		}

		template <typename T, typename... Args>
		T* NewNode(Args&&... args)
		{
			auto n = std::unique_ptr<T>(new T(std::forward<Args>(args)...));
			T* raw = n.get();
			raw->origin = location;
			m_Nodes.push_back(std::move(n));
			return raw;
		}

		uint32 SlotCount() const { return static_cast<uint32>(slots.size()); }
		const Slot& GetSlot(uint32 i) const { return slots[i]; }
		void SetStaticConstant(uint32 i, const ConstantNode* value) { slots[i].initial = value; }
		void SetStorageModifiers(uint32 i, bool isVolatile, bool isThreadLocal) { slots[i].isVolatile = isVolatile; slots[i].isThreadLocal = isThreadLocal; }
	};

	/// While it lives, the nodes and slots `function` makes carry `at` (the statement or clause being lowered);
	/// then the location before it is back. A location without a line leaves the outer one.
	class LoweringAt
	{
	public:
		LoweringAt(HIRFunction& function, const SourceLocation& at) : m_Function(function), m_Outer(function.location)
		{
			if (at.line != 0) function.location = at;
		}
		~LoweringAt() { m_Function.location = m_Outer; }
		LoweringAt(const LoweringAt&) = delete;
		LoweringAt& operator=(const LoweringAt&) = delete;

	private:
		HIRFunction& m_Function;
		SourceLocation m_Outer;
	};
} // namespace Scripting
