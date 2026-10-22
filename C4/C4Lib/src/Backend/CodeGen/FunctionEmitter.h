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

		/// A copy of `hir`'s function, defined in another partition of a split program (D-81), for the optimizer to
		/// inline and then drop (`available_externally`): its static storage, guards and nested functions are the
		/// defining partition's, declared here.
		void EmitImport(const HIRFunction& hir);

	private:
		void EmitBody(const HIRFunction& hir);
		void PrepareDefinition(llvm::Function& fn);

		ModuleContext& m_MCtx;
	};
} // namespace Scripting
