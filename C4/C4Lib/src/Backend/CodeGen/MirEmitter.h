#pragma once
#include "Backend/CodeGen/EhLowering.h"
#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/IRContext.h"
#include "Backend/MIR/MirBody.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	/// Emits one function's MIR into its llvm::Function (docs/compiler/06-mir.md "Emission"): every block control can
	/// reach from the entry, each block's statements through the ExprEmitter, its terminator as a branch, a switch or a
	/// return. A block that is its predecessor's only way on continues in that block's LLVM block. A region runs where
	/// its statement's expression names it; the target's EhLowering makes an unwind step's blocks the first time a call
	/// needs them.
	class MirEmitter : public BlockServices
	{
	public:
		MirEmitter(IRContext& ctx, const MirBody& mir);
		~MirEmitter() override;

		/// The body, starting at the insert point (the function's entry block, after its prologue).
		void Emit();

		// BlockServices: what a call and an expression need of the block being emitted.
		llvm::BasicBlock* UnwindTarget() override;
		llvm::FuncletPadInst* CurrentFunclet() override;
		void EmitNestedBlock(const BlockNode* block) override;

		// For the EhLowering.
		IRContext& Context() { return m_Ctx; }
		ExprEmitter& Expr() { return m_Expr; }
		const MirBody& Mir() const { return m_Mir; }
		/// The LLVM block of a block an unwind step or a dispatch goes to (a cleanup copy's first block, a handler's
		/// entry): made on first need and emitted later.
		llvm::BasicBlock* BlockOf(MirBlockId block);
		llvm::BasicBlock* NewBlock(const char* name);
		/// A once region's guard: the global (a thread_local's address, read where the region begins).
		llvm::Value* OnceGuard(const OnceNode* once);

	private:
		/// A region being emitted: its blocks' LLVM blocks (a region is emitted each time its expression is: a few blocks,
		/// looked up in order), the ones left to emit, and where it ended.
		struct RegionEmission
		{
			std::vector<std::pair<MirBlockId, llvm::BasicBlock*>> blocks;
			std::vector<MirBlockId> pending;
			llvm::BasicBlock* end = nullptr;
			llvm::BasicBlock* Find(MirBlockId block) const;
		};
		/// What the emitter keeps of each MIR block.
		struct BlockState
		{
			llvm::BasicBlock* llvm = nullptr;    // made on first need
			uint32 predecessors = 0;             // the terminators that go to it
			MirStepId cleanupOf = c_MirNone;     // the cleanup step whose copy it begins
			bool apart = false;                  // entered otherwise than by a terminator (a handler's entry, a cleanup copy's
			                                     // first block, a region's)
		};

		/// The LLVM block of a block a terminator goes to: the region's own inside a region.
		llvm::BasicBlock* TargetOf(MirBlockId block);
		bool Fuses(MirBlockId block) const;
		void MarkEmitted(MirBlockId block, llvm::BasicBlock* at);
		/// Emits `block` at the insert point, and each block it is the only way into after it.
		void EmitFrom(MirBlockId block);
		/// The block's statements, then its terminator; returns the block it continues into, or c_MirNone.
		MirBlockId EmitBlock(MirBlockId block);
		void EmitStatement(const MirStatement& statement);
		void EmitDeclare(const LocalDeclNode* declaration);
		void EmitStageReturn(const HIRNode* value);
		void EmitOnceEnd(const OnceNode* once);
		void EmitAtExit(const AtExitNode* node);
		MirBlockId EmitTerminator(const MirBlock& block);
		void EmitReturn(const HIRNode* value);
		void EmitReturnValue(llvm::Value* value);
		void EmitFallOff();
		void EmitOnceBegin(const MirTerminator& end);
		void EmitThreadOnceBegin(const MirTerminator& end);
		const Type* ReturnType() const;
		bool IsOpen() const;
		void TerminateOpenBlocks();

		IRContext& m_Ctx;
		ExprEmitter m_Expr;
		const MirBody& m_Mir;
		std::unique_ptr<EhLowering> m_Eh;
		std::vector<BlockState> m_Blocks; // by MIR block
		std::vector<MirBlockId> m_Pending;
		std::vector<RegionEmission*> m_Regions;  // the regions being emitted, innermost last
		std::vector<std::pair<uint32, uint32>> m_StatementRegions; // the regions of the statements being emitted
		MirBlockId m_Current = 0;
		llvm::Type* m_StagedType = nullptr;      // what the returns stage in the return slot; null when nothing is
		std::vector<std::pair<const OnceNode*, llvm::Value*>> m_OnceGuards;
	};
} // namespace Scripting
