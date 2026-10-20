#include "Backend/HIR/IndexLoop.h"

namespace Scripting
{
	void AppendIndexLoop(LoweringContext& ctx, BlockNode* region, const IndexRange& range, bool reverse,
		const std::function<void(BlockNode*, const HIRNode*)>& visit)
	{
		TypeStore* ts = ctx.queries().TypeStore();
		const Type* i64 = ts->Builtin(TypeKind::Int64);
		const Type* boolT = ts->Builtin(TypeKind::Bool);
		const uint32 slot = ctx.AllocSlot(i64, "__element", SlotClass::Temp);
		LValue indexLv; indexLv.kind = LValue::Kind::Slot; indexLv.type = i64; indexLv.slot = slot;
		const LValue* index = ctx.NewLValue(indexLv);
		auto load = [&]() { return ctx.out->NewNode<LoadNode>(i64, index); };
		region->stmts.push_back(ctx.out->NewNode<LocalDeclNode>(slot, i64, reverse ? range.end() : range.begin()));
		const HIRNode* cond = reverse ? ctx.out->NewNode<IntrinsicOpNode>(boolT, IntrinsicOp::Gt, load(), range.begin())
			: ctx.out->NewNode<IntrinsicOpNode>(boolT, IntrinsicOp::Lt, load(), range.end());
		auto* loop = ctx.out->NewNode<WhileNode>(cond);
		loop->breakLabel = ctx.NewLabel();
		loop->continueLabel = ctx.NewLabel();
		loop->body = ctx.out->NewNode<BlockNode>(nullptr);
		auto step = [&](IntrinsicOp op) {
			const HIRNode* one = ctx.out->NewNode<ConstantNode>(i64, ConstantValue::Int(1));
			loop->body->stmts.push_back(ctx.out->NewNode<StoreNode>(index, ctx.out->NewNode<IntrinsicOpNode>(i64, op, load(), one)));
		};
		if (reverse) step(IntrinsicOp::Sub);
		visit(loop->body, load());
		if (!reverse) step(IntrinsicOp::Add);
		region->stmts.push_back(loop);
	}
} // namespace Scripting
