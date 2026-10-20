#pragma once
#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/CodeGen/CleanupStack.h"
#include "Backend/HIR/HIRNodes.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <vector>
#include <unordered_map>

namespace Scripting
{
	/// What expression emission needs from the region emitter, which owns blocks and cleanups: a cleanup's
	/// `cleanuppad` the first time an exception edge needs it, and the statements of a block that is a step
	/// of an expression (a loop over an array's elements).
	class RegionServices
	{
	public:
		virtual ~RegionServices() = default;
		virtual llvm::BasicBlock* CleanupPadFor(size_t index) = 0;
		virtual void EmitNestedBlock(const BlockNode* block) = 0;
	};

	/// Per-function emission state (doc 03): the IRBuilder, the slot storage, `this`, and the cleanup stack
	/// that decides how every call unwinds. Created per function, dies after it emits. The builder's insert
	/// point is never assumed - every emitter sets it before creating anything.
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
		// One alloca per slot in the entry block, zero-initialized (static locals are module globals).
		void CreateSlotAllocas();
		// Zero a local slot again (with its vptrs): a declaration without an initializer, on every pass.
		void ResetSlot(uint32 slot);
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

		// --- scope exit and unwinding (docs/compiler/04-codegen-and-abi.md) ------------------------------
		CleanupStack& Cleanups() { return m_Cleanups; }
		void SetRegionServices(RegionServices* region) { m_Region = region; }
		void EmitNestedBlock(const BlockNode* block) { m_Region->EmitNestedBlock(block); }
		// Where an exception raised at the current point goes: the innermost cleanup's pad or try's
		// catchswitch above the floor, or null (unwind to the caller).
		llvm::BasicBlock* UnwindTarget() { return UnwindTargetBelow(m_Cleanups.Depth(), m_Floor); }
		llvm::BasicBlock* UnwindTargetBelow(size_t index, size_t floor);
		// The funclet the insert point is in (a catchpad, or the cleanuppad of a cleanup being emitted for
		// unwinding), or null. Every call made inside a funclet carries it as the "funclet" bundle.
		llvm::FuncletPadInst* CurrentFunclet() const;
		llvm::CatchPadInst* CurrentHandlerPad() const { return m_Cleanups.InnermostHandler(m_Floor); }
		// While a cleanup body is emitted inside its cleanuppad, nothing below `floor` is an unwind target.
		struct FuncletState { size_t floor = 0; llvm::FuncletPadInst* funclet = nullptr; };
		FuncletState EnterCleanupFunclet(size_t floor, llvm::FuncletPadInst* pad);
		void LeaveCleanupFunclet(const FuncletState& saved) { m_Floor = saved.floor; m_FloorFunclet = saved.funclet; }
		size_t Floor() const { return m_Floor; }

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
		llvm::Value* m_This = nullptr;
		llvm::DISubprogram* m_DebugScope = nullptr;
		llvm::Value* m_Sret = nullptr;
		CleanupStack m_Cleanups;
		RegionServices* m_Region = nullptr;
		size_t m_Floor = 0;
		llvm::FuncletPadInst* m_FloorFunclet = nullptr;
		llvm::AllocaInst* m_ReturnSlot = nullptr;
		std::unordered_map<const Type*, llvm::Value*> m_ThrowSlots;
	};

	/// RAII insert-point guard: moves the builder to `blk` and restores the previous block on destruction.
	class InsertGuard
	{
	public:
		InsertGuard(llvm::IRBuilder<>& b, llvm::BasicBlock* blk) : m_B(b), m_Prev(b.GetInsertBlock())
		{
			if (blk) b.SetInsertPoint(blk);
		}
		~InsertGuard() { if (m_Prev) m_B.SetInsertPoint(m_Prev); }
	private:
		llvm::IRBuilder<>& m_B;
		llvm::BasicBlock* m_Prev;
	};
} // namespace Scripting
