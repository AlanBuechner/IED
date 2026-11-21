#pragma once
#include "Backend/CodeGen/EhLowering.h"
#include <vector>

namespace Scripting
{
	/// Exceptions as landing pads on libgcc's unwinder (Linux, D-86, docs/compiler/04 "Landing pads"): an exception
	/// raised with step `s` first lands at s's landingpad, whose clauses are those of every dispatch on its way out
	/// (innermost first, up to a catch-all), then takes each step in turn - a cleanup's copy runs, a once region gives up
	/// its guard, a handler ends its catch, a dispatch picks a clause by the selector - and below the last one resumes
	/// unwinding in the caller. The tables a throw and a catch name are the same as Windows' (EhTables).
	class LandingPadLowering : public EhLowering
	{
	public:
		explicit LandingPadLowering(MirEmitter& emitter);

		llvm::BasicBlock* UnwindBlock(MirStepId step) override;
		llvm::FuncletPadInst* Pad(MirFuncletId) override { return nullptr; }
		void BeginHandler(uint32 handler) override;
		void BeginCleanup(MirStepId) override {}
		void LeaveHandler(uint32 handler, llvm::BasicBlock* target) override;
		void Unwind(MirFuncletId funclet, MirStepId next) override;
		void Throw(const ThrowNode* node, uint32 handler) override;
		void Terminate() override;

	private:
		/// Step `step`'s part of an exception's way out, made once.
		llvm::BasicBlock* StepBlock(MirStepId step);
		llvm::BasicBlock* StepFrom(MirStepId step);
		void EmitDispatch(const MirStep& dispatch);
		llvm::BasicBlock* ResumeBlock();
		llvm::Value* ExceptionSlot();
		llvm::Value* SelectorSlot();
		/// The TypeDescriptor a clause catches (null: everything).
		llvm::Constant* CaughtType(const MirHandler& handler);
		llvm::Function* EhRuntime(const char* name, llvm::Type* result, std::vector<llvm::Type*> params, bool raises = false);
		void EndCatch();

		MirEmitter& m_Emitter;
		std::vector<llvm::BasicBlock*> m_LandingPads; // by step
		std::vector<llvm::BasicBlock*> m_Steps;       // by step
		llvm::BasicBlock* m_Resume = nullptr;
		llvm::Value* m_ExceptionSlot = nullptr; // the exception in flight and its selector
		llvm::Value* m_SelectorSlot = nullptr;
	};
} // namespace Scripting
