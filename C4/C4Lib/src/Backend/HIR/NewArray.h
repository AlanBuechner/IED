#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	struct NewExpressionNode;
	class ExprLowerer;

	/// `new T[n]`, `new T[n]{ ... }` and `new (storage) T[n]` (docs/language/10): one allocation of n zeroed
	/// elements; the braced entries start the first elements and the rest are default-constructed. A count that
	/// is negative, too large to allocate, or below the elements the braces name throws the prelude's
	/// BadArrayLength. When starting an element throws, the elements already started are destroyed, last first.
	class NewArrayLowerer
	{
	public:
		NewArrayLowerer(LoweringContext& ctx, ExprLowerer& expr) : m_Ctx(ctx), m_Expr(expr) {}

		/// `element` is the type after `new` without its outer `[n]`; `placement` the storage, or null for the heap.
		QueryResult<Value> Lower(const NewExpressionNode* n, const Type* element, const HIRNode* placement);

	private:
		QueryResult<const HIRNode*> Count(const NewExpressionNode* n);
		QueryResult<uint64> ElementsNamed(const NewExpressionNode* n, const Type* element);
		QueryResult<BlockNode*> LengthCheck(const NewExpressionNode* n, const LValue* count, uint64 minimum, const Type* element);
		QueryResult<BlockNode*> Initialize(const NewExpressionNode* n, const Type* element, const LValue* elements,
			const LValue* count, uint64 named);
		QueryResult<void> StartNamed(const NewExpressionNode* n, const Type* element, const LValue* elements, uint64 named, BlockNode* region);
		QueryResult<void> StartRest(const NewExpressionNode* n, const Type* element, const LValue* elements, const LValue* count,
			const LValue* started, uint64 named, BlockNode* region);
		/// The block an element's start runs in: an exception leaving it destroys elements [0, started), last first.
		BlockNode* StartedGuard(BlockNode* region, const Type* element, const LValue* elements, const LValue* started);
		const FunctionRecord* Finalizer(const Type* element);

		const LValue* ElementAt(const LValue* elements, const Type* element, const HIRNode* index);
		const LValue* NewSlot(const Type* type, const char* name);
		const HIRNode* Int64(int64 value);
		const HIRNode* LoadOf(const LValue* location);

		LoweringContext& m_Ctx;
		ExprLowerer& m_Expr;
	};
} // namespace Scripting
