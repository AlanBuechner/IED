#pragma once
#include "Backend/CodeGen/FunctionEmitter.h"
#include "Backend/CodeGen/ModuleContext.h"
#include <llvm/IR/LLVMContext.h>

namespace Scripting
{
	/// A partition of a split program other than the first (D-81): an LLVM context of its own, the module context
	/// that fills its module, and the emitter of its functions.
	class PartitionEmitter
	{
	public:
		PartitionEmitter() : m_MCtx(m_Context), m_Emitter(m_MCtx) {}

		ModuleContext& mctx() { return m_MCtx; }
		FunctionEmitter& emitter() { return m_Emitter; }

	private:
		llvm::LLVMContext m_Context; // outlives the module m_MCtx owns
		ModuleContext m_MCtx;
		FunctionEmitter m_Emitter;
	};
} // namespace Scripting
