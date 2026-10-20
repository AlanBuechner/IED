#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Backend/HIR/JumpScopes.h"
#include "Backend/HIR/ModelQueries.h"
#include "Compiler/ErrorTracker.h"
#include "Model/TypeHandle.h"
#include <unordered_map>

namespace Scripting
{
	class ExprLowerer; // lowers expressions (its own pair)
	class StmtLowerer;  // lowers statements (its own pair)

	// =====================================================================
	// LOWERING CONTEXT - the per-function state. TRANSIENT: it lives for exactly one LowerFunction
	// call and owns no pointers into the AST beyond the node currently being lowered, so two
	// functions lowered back-to-back cannot interfere. It composes ModelQueries (the only model-
	// facing surface) and hands itself to ExprLowerer / StmtLowerer - the two walks. The ONLY entry
	// points are ResolveExpr / LowerStmt; every node method talks to the model through queries, so
	// there is exactly one error channel and one place that knows who is asking (unitId + type).
	// =====================================================================
	class LoweringContext
	{
	public:
		LoweringContext(ProgramModel* model, ErrorTracker* errors) : m_Model(model), m_Errors(errors), m_Queries(model) {}

		// --- the function being lowered (set by LowerFunction) ------------------------------
		const FunctionRecord* fn = nullptr;          // signature + body + containing type
		HIRFunction* out = nullptr;                  // the tree being built (arena owner)
		uint32 unitId = 0;                           // defining file of `fn` (visibility domain)
		const ObjectType* enclosingType = nullptr;   // implicit this for member functions (an object receiver)
		const Type* primitiveThis = nullptr;         // implicit this when the receiver is a PRIMITIVE (extension method `extends float`): this is a pointer to the value, *this loads it
		const Scope* scope = nullptr;                // fn's enclosing namespace/class: unqualified type lookup context
		const SymbolInfo* startedGlobal = nullptr;   // the global whose start is lowered (no fn)

		/// The symbol of the code being lowered (the function, or the global being started): its lambdas are named for it.
		Core::String CodeSymbol() const { return fn ? fn->mangledName : startedGlobal ? startedGlobal->mangledName : Core::String("__c4_global"); }

		// --- a lambda's call method (docs/language/07): set by EnterClosureBody --------------------------
		const LValue* closureObject = nullptr; // the closure the captures live in (the call method's own `this`)
		const LValue* capturedThis = nullptr;  // the enclosing object, when the lambda captures `this` or `*this`

		// The object `this` and unqualified members name: the receiver, or in a lambda the captured object.
		/// A static method has no object: no `this`, and no implicit receiver for a field or a method.
		bool InStaticMethod() const { return !capturedThis && fn && fn->isStatic && (fn->containingType || fn->primitiveReceiverType); }
		/// `'x' ...: the static method 'Get' has no object` for what needs one.
		Core::String NoObject(const Core::String& what) const { return what + ": the static method '" + fn->name + "' has no object"; }

		const LValue* ReceiverLValue()
		{
			if (capturedThis) return capturedThis;
			LValue lv; lv.kind = LValue::Kind::This; lv.type = enclosingType;
			return NewLValue(lv);
		}

		ModelQueries& queries() { return m_Queries; }
		ProgramModel& model() { return *m_Model; }
		ErrorTracker& errors() { return *m_Errors; }

		// --- slots ---------------------------------------------------------------------------
		uint32 AllocSlot(const Type* t, const Core::String& name, SlotClass c) { return out->AddSlot(t, name, c); }
		LValue* NewLValue(const LValue& v) { return out->NewLValue(v); }

		// The one way a parameter or body local enters scope: register `name` in the current layer, bound to
		// `slot`. A reference local's slot holds the referent's address (its symbol type is a pointer to the
		// referent); reading it goes through that address. False when the name is already in this layer.
		bool BindLocal(const Core::String& name, SymbolInfo sym, uint32 slot, bool isReference = false, bool refersToConst = false)
		{
			sym.localSlot = slot;
			sym.localIsReference = isReference;
			sym.localRefersToConst = refersToConst;
			return m_Queries.RegisterLocal(name, sym);
		}

		// A lambda capture enters the body's scope bound to its closure field (a reference capture's field holds
		// the variable's address). The closure object is read-only in a call operator that is not `mutable`.
		bool BindCapture(const Core::String& name, SymbolInfo sym, uint32 field, bool isReference)
		{
			sym.localCaptureField = field;
			sym.localIsReference = isReference;
			return m_Queries.RegisterLocal(name, sym);
		}

		// The location a local names: its slot (or closure field), or for a reference local the referent behind it.
		Value LocalValue(const SymbolInfo& local)
		{
			const LValue* holder = local.localCaptureField != SymbolInfo::kNoSlot ? CaptureField(local) : SlotOf(local);
			if (!local.localIsReference) return Value::L(holder);
			LValue deref; deref.kind = LValue::Kind::Deref;
			deref.type = static_cast<const PointerType*>(local.type)->GetPointeeType();
			deref.readOnly = local.localRefersToConst;
			deref.addr = LoadIfNeeded(Value::L(holder));
			return Value::L(NewLValue(deref));
		}

		const LValue* SlotOf(const SymbolInfo& local)
		{
			LValue slot; slot.kind = LValue::Kind::Slot; slot.type = local.type; slot.slot = local.localSlot;
			slot.readOnly = local.isConst && !local.localIsReference;
			return NewLValue(slot);
		}

		const LValue* CaptureField(const SymbolInfo& local)
		{
			const auto* closure = static_cast<const ObjectType*>(closureObject->type);
			const ObjectType::FieldInfo& field = closure->GetFields()[local.localCaptureField];
			LValue lv; lv.kind = LValue::Kind::Field; lv.type = field.m_Type; lv.base = closureObject; lv.field = &field;
			return NewLValue(lv);
		}

		// --- lvalue -> rvalue coercion: the ONE place an addressable location becomes a qty --
		const HIRNode* LoadIfNeeded(const Value& v)
		{
			if (!v.isLValue) return v.rvalue;
			// `kCount` stays an lvalue (`&kCount` is its storage), but reading it is its value.
			if (v.lvalue->kind == LValue::Kind::Global && v.lvalue->sym && v.lvalue->sym->IsFoldedConstant())
				return out->NewNode<ConstantNode>(v.type, v.lvalue->sym->constValue);
			return out->NewNode<LoadNode>(v.type, v.lvalue);
		}

		// --- pointer-like coercion: the ONE place a value is converted to fit a store/comparison --
		// Pointer and function-pointer values are interchangeable at the bit level (a funcptr IS an
		// address), so `void*` (null) converts to any of them, and a T* converts to void*. This is the
		// single rule every assignment / initializer / comparison uses, so the three forms can never
		// diverge again (the FunPtrNullInit corpus test locks init/assign/compare consistency). Returns
		// `node` unchanged when no conversion is needed. CONST IS PRESERVED: a `const T*` will not be
		// implicitly converted to a mutable `T*` (that would let caller code write through a pointer the
		// callee promised read-only - e.g. a reflected name). Adding const (`T*` -> `const T*`) is free.
		const HIRNode* CoerceTo(const HIRNode* node, const Type* target)
		{
			if (!node || !target || node->type == target) return node;
			if (const HIRNode* decayed = DecayArray(node, target)) return decayed;
			bool fromPointerLike = node->type && (node->type->GetKind() == TypeKind::Pointer || node->type->GetKind() == TypeKind::FunPtr);
			bool toPointerLike = target->GetKind() == TypeKind::Pointer || target->GetKind() == TypeKind::FunPtr;
			if (!fromPointerLike || !toPointerLike) return node; // not a pointer conversion: leave it (numeric/common-type rules are the caller's)
			if (DropsConst(node->type, target)) return node;     // refusing to drop const: the caller reports a clean mismatch
			return out->NewNode<ConvertNode>(node, node->type, target);
		}

		// An array read where a pointer to its element (or `void*`) is wanted: the address of its first
		// element, as C decays it. Null when `node` is not an array lvalue's value or `target` does not fit.
		const HIRNode* DecayArray(const HIRNode* node, const Type* target)
		{
			if (node->kind != HIRNode::Kind::Load || !node->type || node->type->GetKind() != TypeKind::Array) return nullptr;
			const Type* to = TypeHandle::UnwrapToValue(target);
			if (!to || to->GetKind() != TypeKind::Pointer) return nullptr;
			const Type* elem = TypeHandle::UnwrapToValue(static_cast<const ArrayType*>(node->type)->GetElementType());
			const Type* pointee = TypeHandle::UnwrapToValue(static_cast<const PointerType*>(to)->GetPointeeType());
			if (pointee != elem && !(pointee && pointee->GetKind() == TypeKind::Void)) return nullptr;
			const Type* elemPtr = queries().TypeStore()->Pointer(elem);
			const HIRNode* first = out->NewNode<AddressOfNode>(elemPtr, static_cast<const LoadNode*>(node)->loc);
			return elemPtr == target ? first : out->NewNode<ConvertNode>(first, elemPtr, target);
		}

		// Does converting `from` to `to` require DROPPING const on the pointee? Only meaningful for a
		// pointer -> pointer conversion with an identical (non-void) pointee: the source pointee is
		// read-only and the target's is writable. void* is universal (no const notion), and funcptrs have
		// no const-referent, so those never drop const. Used by CoerceTo/CoerceValue to refuse the
		// conversion; the call sites then report a clean "cannot drop const" error.
		static bool DropsConst(const Type* from, const Type* to)
		{
			if (!from || !to) return false;
			if (from->GetKind() != TypeKind::Pointer || to->GetKind() != TypeKind::Pointer) return false;
			const auto* f = static_cast<const PointerType*>(from);
			const auto* t = static_cast<const PointerType*>(to);
			const Type* fp = f->GetPointeeType();
			const Type* tp = t->GetPointeeType();
			if (!fp || !tp) return false;
			if (fp->GetKind() == TypeKind::Void || tp->GetKind() == TypeKind::Void) return false; // void* universal: no const notion
			return fp == tp && f->IsConstReferent() && !t->IsConstReferent();
		}

		// Full implicit-coercion for a value stored into a known target type: pointer/funcptr coercion
		// (CoerceTo) PLUS numeric conversion. The language allows an implicit numeric conversion whenever
		// both sides are numeric (widening AND narrowing, e.g. int32 -> uint32 / int64 and float -> int32),
		// the same usual-arithmetic rule binary ops and `return` apply. Without this, a local initializer or
		// simple assignment whose value type differs from the slot type (e.g. `int32 c = <float expr>`)
		// would store the raw bits with no conversion - silently wrong. Returns node unchanged when neither
		// rule applies (the caller then reports a genuine mismatch).
		// The address of `lv` as a `pointerType` value (a reference binding): a derived object's address
		// converts to the base sub-object the pointer names.
		const HIRNode* AddressAs(const Type* pointerType, const LValue* lv, bool transient = false)
		{
			const Type* own = queries().TypeStore()->Pointer(TypeHandle::UnwrapToValue(lv->type));
			auto* addr = out->NewNode<AddressOfNode>(own, lv);
			addr->transient = transient;
			return own == pointerType ? static_cast<const HIRNode*>(addr) : out->NewNode<ConvertNode>(addr, own, pointerType);
		}

		const HIRNode* CoerceValue(const HIRNode* node, const Type* target)
		{
			if (!node || !target || node->type == target) return node;
			if (const HIRNode* decayed = DecayArray(node, target)) return decayed;
			bool fromPointerLike = node->type && (node->type->GetKind() == TypeKind::Pointer || node->type->GetKind() == TypeKind::FunPtr);
			bool toPointerLike = target->GetKind() == TypeKind::Pointer || target->GetKind() == TypeKind::FunPtr;
			if (fromPointerLike && toPointerLike)
			{
				if (DropsConst(node->type, target)) return node; // do not drop const: the caller reports a clean mismatch
				return out->NewNode<ConvertNode>(node, node->type, target);
			}
			if (node->type && node->type->IsNumeric() && target->IsNumeric()) return out->NewNode<ConvertNode>(node, node->type, target);
			return node;
		}

		// --- temporaries (docs/language/06): objects a full expression creates and does not keep ------------
		// Each is a hidden slot its statement zeroes first and destroys when the statement ends (LowerStmt
		// places both); a condition or loop step destroys its own as soon as it is computed.
		struct PendingTemporary { uint32 slot = 0; const Type* type = nullptr; };
		std::vector<PendingTemporary> pendingTemporaries;

		// --- last-use moves (D-05, Backend/HIR/LastUseMoves) ------------------------------------
		// Every copy of a local's or by-value parameter's object, and each such variable's scope-exit cleanup.
		struct TransferSite { TransferNode* node = nullptr; uint32 slot = 0; };
		std::vector<TransferSite> transfers;
		// A variable begins at its LocalDeclNode, or - a parameter, a catch variable - at the start of `beginsWith`.
		struct LocalCleanup { DeferNode* cleanup = nullptr; BlockNode* beginsWith = nullptr; };
		std::unordered_map<uint32, LocalCleanup> localCleanups;
		void NoteCleanup(uint32 slot, DeferNode* cleanup, BlockNode* beginsWith = nullptr)
		{
			if (cleanup) localCleanups[slot] = { cleanup, beginsWith };
		}

		// --- break/continue labels (04.1): target the INNERMOST enclosing region -----------
		// `break` exits the nearest enclosing LOOP or SWITCH; `continue` jumps to the step of the
		// nearest enclosing LOOP (an error if only a switch encloses it). Two independent stacks:
		// breaks holds every breakable region (loops push, switches push); continues holds loops only.
		uint32 m_NextLabel = 1;
		std::vector<uint32> m_Breaks;     // innermost last; a loop or switch's exit label
		std::vector<uint32> m_Continues;  // innermost last; a loop's step label
		uint32 NewLabel() { return m_NextLabel++; }
		void PushLoop(uint32& breakL, uint32& contL) { breakL = NewLabel(); contL = NewLabel(); m_Breaks.push_back(breakL); m_Continues.push_back(contL); }
		void PopLoop() { if (!m_Breaks.empty()) m_Breaks.pop_back(); if (!m_Continues.empty()) m_Continues.pop_back(); }
		void PushSwitch(uint32& breakL) { breakL = NewLabel(); m_Breaks.push_back(breakL); }
		void PopSwitch() { if (!m_Breaks.empty()) m_Breaks.pop_back(); }
		bool HasBreakTarget() const { return !m_Breaks.empty(); }
		bool HasContinueTarget() const { return !m_Continues.empty(); }
		uint32 InnerBreak() const { return m_Breaks.back(); }
		uint32 InnerContinue() const { return m_Continues.back(); }

		// --- defer bodies (docs/language/09 section 7): control may not leave a defer body -----
		// While a defer body lowers, deferDepth > 0 and the floors record how many break/continue targets
		// existed outside it, so a break/continue that would leave the body is rejected.
		uint32 deferDepth = 0;
		size_t deferBreakFloor = 0;
		size_t deferContinueFloor = 0;
		bool BreakLeavesDefer() const { return deferDepth > 0 && m_Breaks.size() <= deferBreakFloor; }
		bool ContinueLeavesDefer() const { return deferDepth > 0 && m_Continues.size() <= deferContinueFloor; }

		// --- block path for the goto rules (docs/language/09 section 5) ---------------------
		JumpScopes jumps;

		// --- named labels (goto / label:, doc 04) ------------------------------------------
		// A goto and its label must share ONE id even when the goto comes first (forward reference).
		// Both look up-or-assign the same id here, so order in the source does not matter.
		std::unordered_map<Core::String, uint32> m_NamedLabels;
		uint32 LabelIdFor(const Core::String& name)
		{
			auto it = m_NamedLabels.find(name);
			if (it != m_NamedLabels.end()) return it->second;
			uint32 id = NewLabel();
			m_NamedLabels.emplace(name, id);
			return id;
		}

		/// A warning at `n`: it is reported, and lowering goes on.
		void Warn(const AstNode* n, const Core::String& message) const
		{
			m_Errors->AddError(n ? n->startLocation : SourceLocation(), message, ErrorSeverity::Warning);
		}

		// --- diagnostics (06): one channel; the first failure aborts the function and is its one report --
		template <typename T> QueryResult<T> Fail(const AstNode* n, const QueryResult<T>& r) const
		{
			if (r.Ok() || m_Reported) return r;
			m_Errors->AddError(r.loc.line || !n ? r.loc : n->startLocation, r.message + m_FailureNote);
			m_Reported = true;
			return r;
		}
		// A failure an inner lowering (a lambda body) already reported: this function's own report is suppressed.
		void NoteReported() const { m_Reported = true; }

		/// While alive, a failure reported inside ends with `note`: why the code is lowered where it is.
		class FailureNote
		{
		public:
			FailureNote(const LoweringContext& ctx, const Core::String& note) : m_Ctx(ctx), m_Outer(ctx.m_FailureNote) { ctx.m_FailureNote = note + m_Outer; }
			~FailureNote() { m_Ctx.m_FailureNote = m_Outer; }
			FailureNote(const FailureNote&) = delete;
			FailureNote& operator=(const FailureNote&) = delete;
		private:
			const LoweringContext& m_Ctx;
			Core::String m_Outer;
		};

	private:
		friend class ExprLowerer;
		friend class StmtLowerer;

		ProgramModel* m_Model;   // read-only except the scope API
		ErrorTracker* m_Errors;  // diagnostics sink
		mutable bool m_Reported = false; // set by the first Fail: the layers it propagates through add nothing
		mutable Core::String m_FailureNote;
		ModelQueries m_Queries;  // the model-facing surface (built over m_Model)
	};
} // namespace Scripting
