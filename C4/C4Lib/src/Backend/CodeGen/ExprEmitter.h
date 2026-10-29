#pragma once
#include "Backend/CodeGen/IRContext.h"
#include <functional>
#include <vector>

namespace llvm { class Value; class Constant; class MDNode; }

namespace Scripting
{
	/// Emits rvalue HIR nodes to SSA values (doc 03). Also owns the lvalue -> address resolution every load,
	/// store, and address-of builds on. Pure with respect to control flow: it never creates basic blocks -
	/// anything that needs a block (ternary joins) is a Region concern. Every create names its element type
	/// from the HIR node's concrete type (the opaque-pointer rule); managed-pointer stores are barrier-guarded
	/// through the one GcStore helper so no call site can forget it.
	/// How a load or store touches memory: a `volatile` variable's is kept as written (D-29), and its alias tag says
	/// which memory it may touch (D-64; null: any).
	struct MemoryAccess { bool isVolatile = false; llvm::MDNode* aliasTag = nullptr; };

	class ExprEmitter
	{
	public:
		explicit ExprEmitter(IRContext& ctx) : m_Ctx(ctx) {}

		// The SSA value of an rvalue node, or nullptr on an emission failure (which reports a diagnostic).
		llvm::Value* Emit(const HIRNode* node);

		// The one home for a store of a typed value (doc 04). The collector needs no barrier today; if a
		// generational or concurrent collector ever does, it is added here and every store gets it.
		llvm::Value* GcStore(llvm::Value* addr, llvm::Value* value, const Type* elemType, MemoryAccess access = {});

		/// Whether `value`, an rvalue of `type`, can be built where it is to be stored, with no temporary of its own: a
		/// call that returns it in memory, or a construction.
		bool BuildsInPlace(const HIRNode* value, const Type* type);
		/// Builds such a `value` at `storage` (copy elision).
		void EmitInPlace(const HIRNode* value, llvm::Value* storage);

		/// An access of `accessed` at `loc`: volatile for a volatile variable or field, or any part of one, and tagged
		/// with the class of memory it touches.
		MemoryAccess AccessOf(const LValue* loc, const Type* accessed) const;

		// The address (opaque ptr) of an lvalue: slot -> its alloca; field / base -> byte-offset GEP from the
		// containing object; global -> the GlobalVariable; this -> the method's this pointer; deref -> the
		// pointer value.
		llvm::Value* LValueAddress(const LValue* loc);

	private:
		IRContext& m_Ctx;

		/// A global's storage; a script thread_local that starts on its first use is started first.
		llvm::Value* GlobalAddress(const SymbolInfo& sym);

		bool MayBeSubobject(const LValue* loc);
		llvm::Value* WithOwnVptrs(llvm::Value* value, const Type* t);
		llvm::Value* StoreKeepingVptrs(llvm::Value* addr, llvm::Value* value, const Type* t, MemoryAccess access);
		bool IsVolatile(const LValue* loc) const;
		llvm::Value* Load(llvm::Type* type, llvm::Value* addr, MemoryAccess access);
		llvm::Value* AdjustObjectPointer(llvm::Value* src, const Type* from, const Type* to);
		llvm::Value* EmitDynamicCast(const DynamicCastNode* n);

		llvm::Value* EmitConstant(const ConstantNode* n);
		llvm::Value* EmitLoad(const LoadNode* n);
		llvm::Value* LoadRun(const LValue* run);
		llvm::Value* RunLaneTable(const LValue* run);
		llvm::Value* DynamicLaneAddress(const LValue* loc);
		llvm::Value* EmitStore(const StoreNode* n);
		llvm::Value* EmitIntrinsicOp(const IntrinsicOpNode* n);
		llvm::Value* EmitConvert(const ConvertNode* n);
		// A normalized integer (snormN/unormN) is stored as intN/uintN but its value is clamped to the
		// normalized domain. Extend the source into signed i64, clamp to [lo,hi], then narrow to the stored
		// width. Called from EmitConvert when the target kind is normalized.
		llvm::Value* EmitAddressOf(const AddressOfNode* n);
		llvm::Value* EmitAddressArithmetic(const AddressArithmeticNode* n);
		llvm::Value* EmitFunctionAddress(const FunctionAddressNode* n);
		llvm::Value* EmitVectorBuild(const VectorBuildNode* n);
		llvm::Value* EmitSequence(const SequenceNode* n);
		llvm::Value* EmitTernary(const TernaryNode* n);
		llvm::Value* EmitVa(const VaNode* n);
		llvm::Value* EmitAtomic(const AtomicNode* n);
		/// A SIMD intrinsic (D-62) over its emitted operands `v`.
		llvm::Value* EmitSimd(const SimdNode* n);
		llvm::Value* EmitSimdMemoryOrReduction(const SimdNode* n, const std::vector<llvm::Value*>& v);
		/// `emit(order)` for a constant order; for one only the run time knows, a switch over the orders `allowed`
		/// holds (a bit per AtomicNode::Order; any other value acts as SeqCst), merging what each case made.
		llvm::Value* EmitOrdered(const HIRNode* runtime, AtomicNode::Order constant, uint32 allowed,
			const std::function<llvm::Value*(AtomicNode::Order)>& emit);
		llvm::Value* EmitCompareExchange(const AtomicNode* n, llvm::Value* address, llvm::Align align);
		llvm::Type* AtomicMemoryType(const Type* value, bool comparesBits);
		llvm::Value* ToAtomicMemory(llvm::Value* v, llvm::Type* memory);
		llvm::Value* FromAtomicMemory(llvm::Value* v, const Type* value);
		llvm::Value* VaIntrinsic(const char* name, std::vector<llvm::Value*> args);
	};
} // namespace Scripting
