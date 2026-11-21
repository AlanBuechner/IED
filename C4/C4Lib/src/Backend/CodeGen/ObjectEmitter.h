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
		/// `n`'s object built at `storage`: zeroed, its vptrs set, then initialized.
		void ConstructAt(const ConstructNode* n, llvm::Value* storage);
		llvm::Value* EmitInitList(const InitListNode* n);
		llvm::Value* EmitDelete(const DeleteNode* n);
		llvm::Value* EmitRelocate(const RelocateNode* n);
		/// `gc_type_of(T)`: the address of T's record, its name and pointer map (no census counter).
		llvm::Value* EmitGcType(const GcTypeNode* n);

		/// Zeroed managed storage for one `objectType` (a pointer-free type is never scanned by the collector).
		llvm::Value* Allocate(const Type* objectType);
		/// Registers `finalizer` (a `[operator("delete")]`) to run when the collector reclaims `object`.
		void RegisterFinalizer(llvm::Value* object, const FunctionRecord& finalizer);

	private:
		/// What an array's finalizer walks: `perElement` innermost elements in each element, `stride` bytes apart.
		struct ArrayCount
		{
			uint64 perElement;
			uint64 stride;
		};

		/// The name the census shows for `type`: its QualifiedName, or a lambda's closure by the code it is in.
		std::string CensusName(const Type* type) const;
		/// What one allocation holds: one object of its type, or `new T[n]` of it.
		enum class Shape { Object, Array };
		/// The allocator for `type`, given its record (c4_alloc_typed / _noscan / _precise / _precise_array).
		llvm::Value* AllocateTyped(const Type* type, Shape shape, llvm::Constant* census, llvm::Value* size, uint64 align);
		llvm::Value* EmitNewArray(const NewNode* n);
		llvm::Value* AllocateRun(const NewNode* n, llvm::Value* count, llvm::Value* bytes);
		llvm::Value* ZeroedRun(const NewNode* n, llvm::Value* bytes);
		static ArrayCount Layout(const Type* element);
		llvm::Value* ZeroedPlacement(const NewNode* n);
		void Initialize(const Type* objectType, llvm::Value* storage, const FunctionRecord* ctor, bool aggregateInit,
			const std::vector<const HIRNode*>& args);
		void FillFields(const ObjectType* obj, llvm::Value* storage, const std::vector<const HIRNode*>& args);
		void FillLanes(const ObjectType* vector, llvm::Value* storage, const std::vector<const HIRNode*>& args);

		IRContext& m_Ctx;
		ExprEmitter& m_Expr;
	};
} // namespace Scripting
