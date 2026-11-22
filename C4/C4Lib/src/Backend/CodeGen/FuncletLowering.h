#pragma once
#include "Backend/CodeGen/EhLowering.h"
#include <vector>

namespace C4Compiler
{
	/// Exceptions as MSVC's funclets (Windows, docs/compiler/04 "Exceptions"): a cleanup step is a cleanuppad running
	/// the cleanup's copy, left by cleanupret to what is below it; a dispatch is a catchswitch whose handlers are
	/// catchpads, each left by catchret; a once region's abandon is a cleanuppad calling the runtime. A handler's end is
	/// no step of its own: an exception raised in a handler unwinds where its try's catchswitch does.
	class FuncletLowering : public EhLowering
	{
	public:
		explicit FuncletLowering(MirEmitter& emitter);

		llvm::BasicBlock* UnwindBlock(MirStepId step) override;
		llvm::FuncletPadInst* Pad(MirFuncletId funclet) override;
		void BeginHandler(uint32 handler) override;
		void BeginCleanup(MirStepId step) override;
		void LeaveHandler(uint32 handler, llvm::BasicBlock* target) override;
		void Unwind(MirFuncletId funclet, MirStepId next) override;
		void Throw(const ThrowNode* node, uint32 handler) override;
		void Terminate() override;

	private:
		llvm::Value* ParentPad(MirFuncletId funclet);
		llvm::BasicBlock* Dispatch(MirStepId step);
		llvm::BasicBlock* Abandon(MirStepId step);

		MirEmitter& m_Emitter;
		std::vector<llvm::BasicBlock*> m_Made;           // by step: a dispatch's or an abandon's block
		std::vector<llvm::CatchSwitchInst*> m_Switches;  // by step: a dispatch's catchswitch
		std::vector<llvm::FuncletPadInst*> m_Pads;       // by funclet: a handler's catchpad, a cleanup's cleanuppad
	};
} // namespace C4Compiler
