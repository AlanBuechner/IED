#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/ReadOnly.h"
#include "Model/TypeHandle.h"

namespace Scripting
{
	const LValue* BuildIndexLValue(LoweringContext& ctx, const Value& base, const HIRNode* indexNode,
		const Type* elemType, size_t elemSize)
	{
		TypeStore* ts = ctx.queries().TypeStore();
		const Type* intT = ts->Builtin(TypeKind::Int64);
		const HIRNode* idx = indexNode;
		if (idx->type != intT) idx = ctx.out->NewNode<ConvertNode>(idx, idx->type, intT);
		const HIRNode* offset = elemSize > 1
			? ctx.out->NewNode<IntrinsicOpNode>(intT, IntrinsicOp::Mul, idx,
				ctx.out->NewNode<ConstantNode>(intT, ConstantValue::Int((int64)elemSize)))
			: idx; // element size 1: the index IS the byte offset

		const HIRNode* baseAddr = nullptr; // a POINTER rvalue to element 0
		if (dynamic_cast<const ArrayType*>(TypeHandle::UnwrapToValue(base.type)))
		{
			// An element of a read-only array is read-only: the element pointer points at const.
			const Type* ptrToElem = ts->Pointer(elemType, base.isLValue && IsReadOnly(ctx, base.lvalue));
			baseAddr = base.isLValue ? ctx.out->NewNode<AddressOfNode>(ptrToElem, base.lvalue) : base.rvalue;
		}
		else // a pointer: the value itself is already element 0's address
			baseAddr = ctx.LoadIfNeeded(base);

		const HIRNode* elemAddr = ctx.out->NewNode<AddressArithmeticNode>(baseAddr->type, baseAddr, offset);
		LValue lv; lv.kind = LValue::Kind::Deref; lv.type = elemType; lv.addr = elemAddr;
		return ctx.NewLValue(lv);
	}

	const LValue* IndexedArray(const LValue* element)
	{
		if (element->kind != LValue::Kind::Deref || !element->addr || element->addr->kind != HIRNode::Kind::AddressArithmetic) return nullptr;
		const HIRNode* base = static_cast<const AddressArithmeticNode*>(element->addr)->base;
		return base && base->kind == HIRNode::Kind::AddressOf ? static_cast<const AddressOfNode*>(base)->loc : nullptr;
	}
} // namespace Scripting
