#pragma once
#include "Backend/CodeGen/IRContext.h"

namespace Scripting
{
	class ExprEmitter;
	class SignatureType;

	/// func<R(A)> values (docs/language/07, docs/compiler/04): { code, environment }. The code is called like a
	/// member function whose `this` is the environment: a closure's call method with a GC copy of the closure,
	/// a one-pointer closure's adapter with that pointer, or the signature's thunk with a funcptr.
	class FuncEmitter
	{
	public:
		FuncEmitter(IRContext& ctx, ExprEmitter& expr) : m_Ctx(ctx), m_Expr(expr) {}

		llvm::Value* EmitMakeFunc(const MakeFuncNode* n);

		/// The LLVM type of the code a func of `signature` holds.
		static llvm::FunctionType* CodeType(ModuleContext& mc, const SignatureType* signature);

	private:
		llvm::Value* HeapEnvironment(const MakeFuncNode* n);
		llvm::Value* Pack(llvm::Value* code, llvm::Value* environment);

		IRContext& m_Ctx;
		ExprEmitter& m_Expr;
	};

	/// The code of a func made from a funcptr of `signature`: calls the funcptr held as its environment.
	llvm::Function* FuncThunk(ModuleContext& mc, const SignatureType* signature);
}
