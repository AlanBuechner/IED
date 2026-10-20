#pragma once
#include "Backend/CodeGen/IRContext.h"

namespace Scripting
{
	class ExprEmitter;

	/// Emits an `asm` statement (docs/language/16) as a call of LLVM inline assembly: the inputs as its arguments,
	/// the outputs as its result (one value, or a struct of them), each stored into its slot.
	class AsmEmitter
	{
	public:
		AsmEmitter(IRContext& ctx, ExprEmitter& expr) : m_Ctx(ctx), m_Expr(expr) {}

		/// Whether control continues after the block (false for a `noreturn` one, which ends in `unreachable`).
		bool Emit(const AsmNode* n);

	private:
		llvm::Type* ResultType(const AsmNode* n);

		IRContext& m_Ctx;
		ExprEmitter& m_Expr;
	};
} // namespace Scripting
