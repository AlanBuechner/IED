#pragma once
#include "Backend/CodeGen/IRContext.h"
#include "Backend/CodeGen/RegionEmitter.h"

namespace Scripting
{
	/// Emits ONE HIRFunction into its llvm::Function (doc 03): builds the signature (via ModuleContext, so it
	/// matches the forward declaration exactly), creates the entry block and slot allocas, binds parameters
	/// and the method's this pointer to their slots, then runs the RegionEmitter over the root block. Nested
	/// functions are emitted as siblings afterwards.
	class FunctionEmitter
	{
	public:
		FunctionEmitter(ModuleContext& mctx) : m_MCtx(mctx) {}

		// Emit `hir`'s body into its (already declared) llvm::Function, and any nested functions it owns.
		void Emit(const HIRFunction& hir);

	private:
		void PrepareDefinition(llvm::Function& fn);

		ModuleContext& m_MCtx;
	};
} // namespace Scripting
