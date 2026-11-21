#pragma once
#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/HIR/HIRNodes.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <vector>
#include <unordered_map>

namespace Scripting
{
	/// What a call and an expression need of the MIR block being emitted (MirEmitter): where a call that may throw
	/// unwinds to, the funclet it runs in, and the region an expression runs in place (a loop over an array's elements).
	class BlockServices
	{
	public:
		virtual ~BlockServices() = default;
		/// Where an exception raised at the insert point goes, or null (the caller).
		virtual llvm::BasicBlock* UnwindTarget() = 0;
		/// The funclet the insert point is in (a catchpad, or the cleanuppad of a cleanup run for an exception), or null.
		virtual llvm::FuncletPadInst* CurrentFunclet() = 0;
		virtual void EmitNestedBlock(const BlockNode* block) = 0;
	};

	/// Per-function emission state (doc 03): the IRBuilder, the slot storage, `this`, and the block being emitted, which
	/// decides how every call unwinds. Created per function, dies after it emits. The builder's insert point is never
	/// assumed - every emitter sets it before creating anything.
	class IRContext
	{
	public:
		IRContext(ModuleContext& mctx, const HIRFunction& hir);

		ModuleContext& mctx() const { return m_MCtx; }
		const HIRFunction& hir() const { return m_Hir; }
		llvm::IRBuilder<>& builder() { return *m_B; }
		void SetInsertPoint(llvm::BasicBlock* blk) { if (m_B) m_B->SetInsertPoint(blk); }

		// The ONE truthiness rule: i1 passes through; a float is != 0.0 (unordered, so NaN is true, as in C); a
		// pointer is non-null (compared with the null POINTER); a func is non-null when its code is; any other
		// integer is != 0 in its own width.
		static llvm::Value* AsBool(llvm::IRBuilder<>& B, llvm::Value* cond)
		{
			if (cond->getType() == llvm::Type::getInt1Ty(B.getContext())) return cond;
			if (cond->getType()->isStructTy()) cond = B.CreateExtractValue(cond, { 0 }); // func: { code, environment }
			if (cond->getType()->isFloatingPointTy())
				return B.CreateFCmpUNE(cond, llvm::ConstantFP::get(cond->getType(), 0.0));
			if (cond->getType()->isPointerTy())
				return B.CreateICmpNE(cond, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(cond->getType())));
			return B.CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
		}

		// --- slots -----------------------------------------------------------------------------------
		// The builder, writing at the end of the function's entry block.
		void StartAt(llvm::BasicBlock& entry);
		// One alloca per slot in the entry block (static locals are module globals); ZeroUndeclaredSlots zeroes them.
		// A slot `held` names (by slot) is that memory instead: a by-value parameter the caller handed over.
		void CreateSlotAllocas(const std::vector<llvm::Value*>& held);
		// Zero a local slot again (with its vptrs): a declaration without an initializer, on every pass.
		void ResetSlot(uint32 slot);
		// After the body: zeroes, with its vptrs, at entry, every local slot no declaration starts (`declared`, by slot,
		// MirBody::declared: a temporary the lifecycle reads, a drop flag): no local ever holds stack garbage. A declared
		// slot starts zeroed or initialized where its declaration runs, and no use comes before it (a goto may not
		// cross a declaration, a case is a block of its own). A parameter is stored at entry.
		void ZeroUndeclaredSlots(const std::vector<bool>& declared);
		llvm::Value* SlotAlloca(uint32 slot) const { return m_SlotAllocas[slot]; }
		bool SlotIsStatic(uint32 i) const { return m_Hir.GetSlot(i).cls == SlotClass::Static; }

		/// The function's debug scope (null without -g): statements then set their line with At.
		void SetDebugScope(llvm::DISubprogram* scope) { m_DebugScope = scope; }
		void At(const SourceLocation& location);

		void SetThis(llvm::Value* v) { m_This = v; }
		llvm::Value* This() const { return m_This; }
		// The hidden result pointer of a function that returns through sret.
		void SetSret(llvm::Value* v) { m_Sret = v; }
		llvm::Value* Sret() const { return m_Sret; }

		// --- temporaries -----------------------------------------------------------------------------
		// Every alloca lives at the top of the entry block. An alloca anywhere else is a dynamic stack
		// allocation (it grows on every loop iteration) and breaks clang's frame proof under -mstackrealign.
		llvm::AllocaInst* EntryAlloca(llvm::Type* ty, const char* name = "tmp");
		// An rvalue stored into a fresh entry-block temporary at the current point; returns its address.
		llvm::Value* Materialize(llvm::Value* v, llvm::Type* ty);
		// The slot a `return` stages its value in while cleanups run (created on first need).
		llvm::AllocaInst* ReturnSlot(llvm::Type* ty);
		// The buffer a `throw` hands to _CxxThrowException; one per thrown type.
		llvm::Value* EnsureThrowSlot(const Type* t);

		// --- the block being emitted (docs/compiler/06-mir.md) -------------------------------------------
		void SetBlockServices(BlockServices* block) { m_Block = block; }
		void EmitNestedBlock(const BlockNode* block) { m_Block->EmitNestedBlock(block); }
		// Where an exception raised at the insert point goes, or null (unwind to the caller).
		llvm::BasicBlock* UnwindTarget() { return m_Block ? m_Block->UnwindTarget() : nullptr; }
		// The funclet the insert point is in, or null. Every call made inside a funclet carries it as the "funclet" bundle.
		llvm::FuncletPadInst* CurrentFunclet() { return m_Block ? m_Block->CurrentFunclet() : nullptr; }

		// --- calls -----------------------------------------------------------------------------------
		// The one place a call instruction is created: adds the funclet bundle, and when the callee may throw
		// and an unwind target exists, emits an invoke and continues at its normal destination.
		llvm::CallBase* Call(llvm::FunctionType* fty, llvm::Value* callee, llvm::ArrayRef<llvm::Value*> args, bool mayThrow);
		llvm::CallBase* Call(llvm::Function* fn, llvm::ArrayRef<llvm::Value*> args, bool mayThrow)
		{
			return Call(fn->getFunctionType(), fn, args, mayThrow);
		}

	private:
		llvm::Value* CreateLocalSlot(const HIRFunction::Slot& slot);
		llvm::Value* CreateStaticSlot(const HIRFunction::Slot& slot);

		ModuleContext& m_MCtx;
		const HIRFunction& m_Hir;
		std::unique_ptr<llvm::IRBuilder<>> m_B; // created in StartAt (needs the entry block first)
		std::vector<llvm::Value*> m_SlotAllocas;
		llvm::Instruction* m_LastSlotAlloca = nullptr; // where entry zeroing goes: after the slots, before the parameters' stores
		llvm::DebugLoc m_PrologueLocation;
		llvm::Value* m_This = nullptr;
		llvm::DISubprogram* m_DebugScope = nullptr;
		llvm::Value* m_Sret = nullptr;
		BlockServices* m_Block = nullptr;
		llvm::AllocaInst* m_ReturnSlot = nullptr;
		std::unordered_map<const Type*, llvm::Value*> m_ThrowSlots;
	};

	/// RAII insert-point guard: moves the builder to `blk` and restores the previous block on destruction.
	/// Emits into another block meanwhile; then the builder is back where it was, at the line it was on (what the other
	/// block emits, a cleanup's statements say, must not lend the next instruction its line: BUG-307).
	class InsertGuard
	{
	public:
		InsertGuard(llvm::IRBuilder<>& b, llvm::BasicBlock* blk) : m_B(b), m_Prev(b.GetInsertBlock()), m_Line(b.getCurrentDebugLocation())
		{
			if (blk) b.SetInsertPoint(blk);
		}
		~InsertGuard()
		{
			if (m_Prev) m_B.SetInsertPoint(m_Prev);
			m_B.SetCurrentDebugLocation(m_Line);
		}
	private:
		llvm::IRBuilder<>& m_B;
		llvm::BasicBlock* m_Prev;
		llvm::DebugLoc m_Line;
	};
} // namespace Scripting
