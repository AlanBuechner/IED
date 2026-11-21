#pragma once
#include "Backend/MIR/MirBody.h"
#include "Target/TargetAbi.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <memory>

namespace Scripting
{
	class MirEmitter;

	/// How a target lowers a function's exceptions (TargetAbi::Exceptions): the MIR's unwind steps, handlers and the
	/// ways out of them as LLVM's funclets (Windows: cleanuppad, catchswitch, catchpad, FuncletLowering) or as landing
	/// pads on the runtime's personality (Linux, D-86: LandingPadLowering). The MirEmitter emits everything else.
	class EhLowering
	{
	public:
		virtual ~EhLowering() = default;

		/// The scheme `scheme` for the function `emitter` emits.
		static std::unique_ptr<EhLowering> For(ExceptionScheme scheme, MirEmitter& emitter);

		/// Where a call that may throw in a block whose exceptions go to `step` unwinds to (null: the caller); its blocks
		/// are made on first need.
		virtual llvm::BasicBlock* UnwindBlock(MirStepId step) = 0;
		/// The funclet pad a call in a block of `funclet` carries (null: none).
		virtual llvm::FuncletPadInst* Pad(MirFuncletId funclet) = 0;
		/// What begins handler `handler`'s entry block, at the insert point: its catchpad, or the runtime's begin_catch.
		virtual void BeginHandler(uint32 handler) = 0;
		/// What begins the first block of cleanup step `step`'s copy, at the insert point (its cleanuppad).
		virtual void BeginCleanup(MirStepId step) = 0;
		/// Handler `handler`'s catch ends, and control goes on at `target`.
		virtual void LeaveHandler(uint32 handler, llvm::BasicBlock* target) = 0;
		/// The end of an unwinding cleanup's copy run in `funclet`: the exception goes on to step `next`.
		virtual void Unwind(MirFuncletId funclet, MirStepId next) = 0;
		/// `throw v`, or a rethrow of handler `handler`'s exception (c_MirNone: no handler is running).
		virtual void Throw(const ThrowNode* node, uint32 handler) = 0;
		/// A noexcept function's handler: the exception stops the program.
		virtual void Terminate() = 0;
	};
} // namespace Scripting
