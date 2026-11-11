#include "Backend/HIR/DestructuringLowerer.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/PackElements.h"
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
		auto shares = Shares(n, list.value);
		if (!shares.Ok()) return QueryResult<void>::Fail(shares.error, shares.message);
		auto start = m_Ctx.Fail(&n, InitialValue(m_Ctx, value.value, list.value));
		if (!start.Ok()) return QueryResult<void>::Fail(start.error, start.message);
		const LValue* held = Hold(start.value, list.value, block);
		uint32 next = 0;
		for (uint32 i = 0; i < n.GetElements().Count(); ++i)
		{
			const DestructuringNode::Element& element = n.GetElements()[i];
			auto r = element.isPack ? DeclarePack(n, element, held, list.value, next, shares.value[i], block)
				: Element(n, element, Moved(held, list.value, next), block);
			if (!r.Ok()) return r;
			next += shares.value[i];
		}
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
		return Result(list);
	}

	QueryResult<std::vector<uint32>> DestructuringLowerer::Shares(const DestructuringNode& n, const ObjectType* list)
	{
		using Result = QueryResult<std::vector<uint32>>;
		const auto& elements = n.GetElements();
		const uint32 values = list->GetFields().Count();
		uint32 packs = 0;
		for (const DestructuringNode::Element& e : elements) packs += e.isPack ? 1u : 0u;
		if (packs > 1) return m_Ctx.Fail(&n, Result::Fail(QueryError::NotAValue, "a destructuring takes at most one pack element"));
		const uint32 fixed = elements.Count() - packs;
		if (packs == 0 ? fixed != values : fixed > values)
			return m_Ctx.Fail(&n, Result::Fail(QueryError::NotAValue, "the destructuring names " + std::to_string(fixed) + " values" +
				(packs ? " besides its pack" : "") + ", but '" + list->GetName() + "' has " + std::to_string(values)));
		std::vector<uint32> shares;
		for (const DestructuringNode::Element& e : elements) shares.push_back(e.isPack ? values - fixed : 1u);
		return shares;
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
		auto type = DeclaredType(element);
		if (!type.Ok()) return QueryResult<void>::Fail(type.error, type.message);
		LocalStart local;
		local.site = &n;
		local.name = element.name.GetText();
		local.type = type.value;
		local.isConst = element.type->DeclaresConstValue();
		return DeclLowerer(m_Ctx).DeclareValue(local, value, block);
	}

	QueryResult<const Type*> DestructuringLowerer::DeclaredType(const DestructuringNode::Element& element)
	{
		auto type = m_Ctx.Fail(element.type.Get(), m_Ctx.queries().ResolveTypeCtx(*element.type, m_Ctx.unitId, m_Ctx.scope));
		if (!type.Ok()) return type;
		if (type.value->GetKind() == TypeKind::Reference)
			return m_Ctx.Fail(element.type.Get(), QueryResult<const Type*>::Fail(QueryError::NotAValue, "a destructured element is a value: '" +
				element.name.GetText() + "' cannot be a reference"));
		return type;
	}

	QueryResult<void> DestructuringLowerer::DeclarePack(const DestructuringNode& n, const DestructuringNode::Element& element, const LValue* held,
		const ObjectType* list, uint32 first, uint32 count, BlockNode* block)
	{
		// `Ts... values` takes a value per element of Ts, each as its element; `int64... rest` the rest, each as an int64.
		const Core::String& name = element.name.GetText();
		const bool perElement = Packs::Names(m_Ctx.model(), element.type.Get(), nullptr);
		PackElements copies;
		if (perElement)
		{
			auto made = PackElements::Of(m_Ctx, *element.type);
			if (!made.Ok()) return QueryResult<void>::Fail(made.error, made.message);
			copies = made.value;
			if (copies.Count() != count)
				return m_Ctx.Fail(element.type.Get(), QueryResult<void>::Fail(QueryError::NotAValue, "the pack '" + name + "' takes " +
					std::to_string(count) + " value(s) of '" + list->GetName() + "', but '" + element.type->ToString() + "' has " +
					std::to_string(copies.Count()) + " element(s)"));
		}
		std::vector<SymbolInfo> locals;
		for (uint32 i = 0; i < count; ++i)
		{
			auto type = perElement ? copies.Within(i, [&] { return DeclaredType(element); }) : DeclaredType(element);
			if (!type.Ok()) return QueryResult<void>::Fail(type.error, type.message);
			LocalStart local;
			local.site = &n;
			local.name = name + "#" + std::to_string(i);
			local.type = type.value;
			local.isConst = element.type->DeclaresConstValue();
			if (auto r = DeclLowerer(m_Ctx).DeclareValue(local, Moved(held, list, first + i), block); !r.Ok()) return r;
			if (const SymbolInfo* declared = m_Ctx.queries().FindLocal(local.name)) locals.push_back(*declared);
		}
		SymbolInfo pack;
		pack.kind = SymbolKind::Variable;
		pack.definingUnit = 0;
		pack.packElements = m_Ctx.model().StorePack(std::move(locals));
		m_Ctx.queries().RegisterLocal(name, pack);
		return QueryResult<void>::Success();
	}

	QueryResult<void> DestructuringLowerer::Assign(const DestructuringNode::Element& element, const Value& value, BlockNode* block)
	{
		auto stored = ExprLowerer(m_Ctx).AssignTo(element.target.Get(), element.target.Get(), value);
		if (!stored.Ok()) return QueryResult<void>::Fail(stored.error, stored.message);
		block->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(Lifecycle(m_Ctx).Discarded(stored.value)));
		return QueryResult<void>::Success();
	}
} // namespace Scripting
