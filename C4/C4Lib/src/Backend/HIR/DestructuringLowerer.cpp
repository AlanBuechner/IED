#include "Backend/HIR/DestructuringLowerer.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Parser/Nodes/Expressions/ValueListNode.h"
#include "Model/TypeHandle.h"
#include <string>

namespace Scripting
{
	QueryResult<void> DestructuringLowerer::Lower(const DestructuringNode& n, BlockNode* block)
	{
		auto value = Source(n);
		if (!value.Ok()) return QueryResult<void>::Fail(value.error, value.message);
		auto list = ListOf(n, value.value);
		if (!list.Ok()) return QueryResult<void>::Fail(list.error, list.message);
		auto start = m_Ctx.Fail(&n, InitialValue(m_Ctx, value.value, list.value));
		if (!start.Ok()) return QueryResult<void>::Fail(start.error, start.message);
		const LValue* held = Hold(start.value, list.value, block);
		for (uint32 i = 0; i < n.GetElements().Count(); ++i)
			if (auto r = Element(n, n.GetElements()[i], Moved(held, list.value, i), block); !r.Ok()) return r;
		return QueryResult<void>::Success();
	}

	QueryResult<Value> DestructuringLowerer::Source(const DestructuringNode& n)
	{
		ExprLowerer expr(m_Ctx);
		if (const auto* list = dynamic_cast<const ValueListNode*>(n.GetValue().Get())) return expr.ResolveValueList(list);
		return expr.ResolveExpr(n.GetValue().Get());
	}

	QueryResult<const ObjectType*> DestructuringLowerer::ListOf(const DestructuringNode& n, const Value& value)
	{
		using Result = QueryResult<const ObjectType*>;
		const Type* type = value.type ? TypeHandle::UnwrapToValue(value.type) : nullptr;
		const auto* list = dynamic_cast<const ObjectType*>(type);
		if (!list || !list->IsReturnList())
			return m_Ctx.Fail(&n, Result::Fail(QueryError::NotAValue, "only a return list destructures: '" +
				(type ? type->GetName() : Core::String("?")) + "' is not one"));
		const uint32 count = n.GetElements().Count();
		if (count != list->GetFields().Count())
			return m_Ctx.Fail(&n, Result::Fail(QueryError::NotAValue, "the destructuring names " + std::to_string(count) + " values, but '" +
				list->GetName() + "' has " + std::to_string(list->GetFields().Count())));
		return Result(list);
	}

	const LValue* DestructuringLowerer::Hold(const HIRNode* start, const ObjectType* list, BlockNode* block)
	{
		// A statement temporary: destroyed, with whatever is left in it, when the statement ends.
		const uint32 slot = m_Ctx.AllocSlot(list, "__destructured", SlotClass::Temp);
		m_Ctx.pendingTemporaries.push_back({ slot, list });
		LValue lv; lv.kind = LValue::Kind::Slot; lv.type = list; lv.slot = slot;
		const LValue* held = m_Ctx.NewLValue(lv);
		block->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(m_Ctx.out->NewNode<StoreNode>(held, start)));
		return held;
	}

	Value DestructuringLowerer::Moved(const LValue* held, const ObjectType* list, uint32 index)
	{
		const ObjectType::FieldInfo& field = list->GetFields()[index];
		LValue lv; lv.kind = LValue::Kind::Field; lv.type = field.m_Type; lv.base = held; lv.field = &field;
		const LValue* location = m_Ctx.NewLValue(lv);
		if (!LifecycleFacts::NeedsDestroy(m_Ctx.queries(), field.m_Type))
			return Value::R(m_Ctx.out->NewNode<LoadNode>(field.m_Type, location), field.m_Type);
		return Value::R(Lifecycle(m_Ctx).Take(location), field.m_Type);
	}

	QueryResult<void> DestructuringLowerer::Element(const DestructuringNode& n, const DestructuringNode::Element& element, const Value& value,
		BlockNode* block)
	{
		switch (element.kind)
		{
		case DestructuringNode::Element::Kind::Declare: return Declare(n, element, value, block);
		case DestructuringNode::Element::Kind::Assign: return Assign(element, value, block);
		default: return QueryResult<void>::Success(); // a discarded value is destroyed with the temporary
		}
	}

	QueryResult<void> DestructuringLowerer::Declare(const DestructuringNode& n, const DestructuringNode::Element& element, const Value& value,
		BlockNode* block)
	{
		auto type = m_Ctx.Fail(element.type.Get(), m_Ctx.queries().ResolveTypeCtx(*element.type, m_Ctx.unitId, m_Ctx.scope));
		if (!type.Ok()) return QueryResult<void>::Fail(type.error, type.message);
		if (type.value->GetKind() == TypeKind::Reference)
			return m_Ctx.Fail(element.type.Get(), QueryResult<void>::Fail(QueryError::NotAValue, "a destructured element is a value: '" +
				element.name.GetText() + "' cannot be a reference"));
		LocalStart local;
		local.site = &n;
		local.name = element.name.GetText();
		local.type = type.value;
		local.isConst = element.type->DeclaresConstValue();
		return DeclLowerer(m_Ctx).DeclareValue(local, value, block);
	}

	QueryResult<void> DestructuringLowerer::Assign(const DestructuringNode::Element& element, const Value& value, BlockNode* block)
	{
		auto stored = ExprLowerer(m_Ctx).AssignTo(element.target.Get(), element.target.Get(), value);
		if (!stored.Ok()) return QueryResult<void>::Fail(stored.error, stored.message);
		block->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(Lifecycle(m_Ctx).Discarded(stored.value)));
		return QueryResult<void>::Success();
	}
} // namespace Scripting
