#pragma once
#include "Backend/CodeGen/IRContext.h"

namespace Scripting
{
	class ExprEmitter;

	/// Emits object creation and destruction (docs/compiler/04): `new T(...)` on the managed heap, `T(...)`
	/// in a stack temporary, `{...}` bound to an `__init_list<T>`, and `delete p`. A new object is zeroed,
	/// gets every vptr of its type, then runs its constructor or has its fields filled in order.
	class ObjectEmitter
	{
	public:
		ObjectEmitter(IRContext& ctx, ExprEmitter& expr) : m_Ctx(ctx), m_Expr(expr) {}

		llvm::Value* EmitNew(const NewNode* n);
		llvm::Value* EmitConstruct(const ConstructNode* n);
		llvm::Value* EmitInitList(const InitListNode* n);
		llvm::Value* EmitDelete(const DeleteNode* n);
		llvm::Value* EmitRelocate(const RelocateNode* n);

		/// Zeroed managed storage for one `objectType` (a pointer-free type is never scanned by the collector).
		llvm::Value* Allocate(const Type* objectType);
		/// Registers `finalizer` (a `[operator("delete")]`) to run when the collector reclaims `object`.
		void RegisterFinalizer(llvm::Value* object, const FunctionRecord& finalizer);

	private:
		llvm::Value* ZeroedPlacement(const NewNode* n);
		void Initialize(const Type* objectType, llvm::Value* storage, const FunctionRecord* ctor, bool aggregateInit,
			const std::vector<const HIRNode*>& args);
		void FillFields(const ObjectType* obj, llvm::Value* storage, const std::vector<const HIRNode*>& args);
		void FillLanes(const ObjectType* vector, llvm::Value* storage, const std::vector<const HIRNode*>& args);

		IRContext& m_Ctx;
		ExprEmitter& m_Expr;
	};
} // namespace Scripting
