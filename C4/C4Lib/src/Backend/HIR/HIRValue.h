#pragma once
#include "Model/Types.h"
#include "Model/ConstantValue.h"
#include "Lexer/Token.h" // SourceLocation
#include <cstdint>

namespace Scripting
{
	struct HIRNode; // a node in the function's tree (HIRNodes.h)
	struct SymbolInfo; // model symbol record (Model/SymbolTable.h); referenced by Global lvalues
	struct FunctionRecord; // model function record (Model/SymbolTable.h); referenced by IndexOp lvalues

	// =====================================================================
	// LVALUES - an addressable location (03.2). An lvalue is NOT a node: it is a
	// compact descriptor that lives in the HIRFunction arena and is referenced by
	// POINTER from Values and nodes. Arena residence is what makes nested lvalues
	// safe: a Field lvalue's `base` points at another arena lvalue, so the whole
	// chain (e.g. this->inner.x) stays valid for the function's lifetime.
	// Codegen walks an LValue to produce the right load/store; HIR never does.
	// =====================================================================
	struct LValue
	{
		enum class Kind { Slot, Field, Base, Global, This, Deref, Lane, Swizzle, VectorLaneDyn, IndexOp };

		Kind kind = Kind::Slot;
		const Type* type = nullptr; // the element type of this location (always concrete, set by the lowerer)

		// Slot: index into HIRFunction::slots.
		uint32 slot = 0;

		// Field: the object lvalue that contains this field + the field descriptor (its offset is relative to
		// the type that declares it). Base: the base sub-object of `base` at `baseOffset` bytes.
		const LValue* base = nullptr;
		const ObjectType::FieldInfo* field = nullptr;
		int64 baseOffset = 0;

		// Global: the model symbol whose storage backs this location.
		const SymbolInfo* sym = nullptr;

		// Deref: `*ptr` - the location behind a pointer (addr is the pointer-producing node). boundBy: the reference
		// field this is the referent of (an assignment in its object's create binds the field instead).
		const HIRNode* addr = nullptr;
		const LValue* boundBy = nullptr;

		// Lane: one scalar element of a packed SIMD vector. base is the ADDRESSABLE lvalue that holds
		// the whole vector (a slot / field / global); laneIndex selects the element. A SIMD lane has no
		// memory address, so Load/Store special-case this kind to extractelement / insertelement.
		uint32 laneIndex = 0;

		// Swizzle: a MULTI-LANE run of a packed SIMD vector (v.xz, v.xyz). base is the lvalue holding
		// the WHOLE <fullLaneCount x T> vector; laneIndices selects (reorders/repeats) the K lanes. The
		// location's type is the sub-vector <K x T>. A swizzle run is not a single memory element, so
		// Load builds it by extractelement per lane and Store writes it back by insertelement per lane.
		std::vector<uint32> laneIndices;
		bool isUnpacked = false; // the run's region is an UNPACKED struct-of-fields (per-field store), not a SIMD vector

		// VectorLaneDyn: a DYNAMICALLY-indexed element of a packed SIMD vector - the whole-vector
		// array alias `a.data[i]` where `data` is `packed T[N]`. base is the lvalue holding the whole
		// <N x T> vector; index is the rvalue that produces the lane number. Load extracts / Store
		// inserts at that runtime index (the SIMD equivalent of a plain array element).
		const HIRNode* index = nullptr;

		// IndexOp: an OVERLOADED `obj[i]` whose [operator("[]")] returns BY VALUE (not a reference).
		// Such an index expression is not a memory location: reading calls the getter, writing calls
		// the [operator("[]=")] setter. There is no address - Load/Store special-case this kind to
		// emit the two method calls instead of load/store. receiver is the object lvalue; idxNode
		// produces the index value; getter/setter are the operator records (setter may be null, in
		// which case a write is ill-formed and the lowerer rejects it).
		const FunctionRecord* getter = nullptr;
		const FunctionRecord* setter = nullptr;
		const LValue* receiver = nullptr;
		const HIRNode* idxNode = nullptr;
		const HIRNode* read = nullptr; // the pre-built getter call (EmitLoad returns it directly)

		// ephemeral: this location is rooted in a BY-VALUE function-call result (a dead temporary). Reading
		// it is fine, but writing through it would store into a temp that is discarded - a silent no-op. The
		// lowerer rejects stores into an ephemeral chain (`GetV().x = 5`, `G().inner.n = 42`).
		bool ephemeral = false;
		// readOnly: a const local's or parameter's slot, or the referent of a reference to const bound to a
		// local. Other read-only locations are recognized from their kind (ReadOnly.h: ReadOnlyReason).
		bool readOnly = false;
	};

	// =====================================================================
	// VALUE - what a resolved expression IS (03.2). Either an rvalue (a quantity,
	// materialized as its producing node) or an lvalue (an addressable location in
	// the arena). The two-ness is first-class: LoadIfNeeded (LoweringContext) is the
	// ONLY place an lvalue is coerced to a quantity, so every call site behaves alike.
	// =====================================================================
	struct Value
	{
		const Type* type = nullptr; // ALWAYS concrete (HIR contract)

		bool isLValue = false;
		const HIRNode* rvalue = nullptr; // rvalue: the node that produces the quantity
		const LValue* lvalue = nullptr;  // lvalue: where the quantity lives (arena pointer)

		static Value R(const HIRNode* node, const Type* t) { Value v; v.type = t; v.rvalue = node; return v; }
		static Value L(const LValue* loc) { Value v; v.type = loc->type; v.isLValue = true; v.lvalue = loc; return v; }

		bool IsRValue() const { return !isLValue; }
	};
} // namespace Scripting
