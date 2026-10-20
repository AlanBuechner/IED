#pragma once
#include "Backend/CodeGen/IRContext.h"

namespace Scripting
{
	class ExprEmitter;

	/// Emits a call (docs/compiler/04): the hidden parameters in MSVC x64 order, each argument passed per
	/// the Abi brick, the callee resolved as direct, virtual (through the receiver's vptr) or indirect
	/// (through a funcptr), and the result unpacked back to its C4 type.
	class CallEmitter
	{
	public:
		CallEmitter(IRContext& ctx, ExprEmitter& expr) : m_Ctx(ctx), m_Expr(expr) {}

		llvm::Value* Emit(const CallNode* n);

		/// Run the constructor `ctor` on the storage at `object` (`new T(args)`, `T(args)`). A create method
		/// that returns its object builds it in place: `object` is also its result pointer.
		void EmitConstructor(const FunctionRecord& ctor, llvm::Value* object, const std::vector<const HIRNode*>& args);

	private:
		struct Callee { llvm::FunctionType* type = nullptr; llvm::Value* target = nullptr; };
		// What the argument path needs to know about a call: a direct callee or a funcptr target, and the
		// argument rvalues in source order.
		struct CallSite
		{
			const FunctionRecord* callee = nullptr;
			const HIRNode* target = nullptr;
			const std::vector<const HIRNode*>& args;
		};

		std::vector<const Type*> ParameterTypes(const CallSite& site) const;
		void AppendArguments(const CallSite& site, std::vector<llvm::Value*>& args);
		llvm::Value* EmitArgument(const CallSite& site, uint32_t i, const Type* paramType, bool variadicExtra);
		/// A C4 `__init_list<T>` (a value or a pointer to one) as the MSVC std::initializer_list a linked C++ callee
		/// takes; null when `paramType` is not an init list.
		llvm::Value* LinkedInitializerList(llvm::Value* list, const Type* paramType);
		llvm::Value* PromoteVariadic(llvm::Value* v, const Type* argType);
		Callee ResolveCallee(const CallNode* n, llvm::Value* thisArg);
		Callee IndirectCallee(const CallNode* n);
		llvm::Value* Result(const CallNode* n, llvm::CallBase* call, llvm::Value* sret, const Type* retType);

		IRContext& m_Ctx;
		ExprEmitter& m_Expr;
	};
} // namespace Scripting
