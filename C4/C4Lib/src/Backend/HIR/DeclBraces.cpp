#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Construction.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/ReadOnly.h"
#include "Model/Abstractness.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include "Model/ConstEvaluator.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/SwizzleType.h"
#include <algorithm>

namespace Scripting
{
	QueryResult<const HIRNode*> DeclLowerer::BracedValue(const BracedInitializerNode* br, const Type* type, const AstNode* site)
	{
		return Lifecycle(m_Ctx).BuildTemporary(type, "__braced", [&](BlockNode* steps, const LValue* temp) {
			return StartFromBraces(br, temp, steps, site, type->GetName());
		});
	}

	QueryResult<void> DeclLowerer::StartFromBraces(const BracedInitializerNode* br, const LValue* target, BlockNode* region,
		const AstNode* site, const Core::String& name)
	{
		const Type* t = target->type;
		// An __init_list<T> is materialized as a {ptr, size} aggregate and stored whole.
		if (t->GetKind() == TypeKind::InitList)
		{
			auto il = m_Ctx.Fail(site, ExprLowerer(m_Ctx).ResolveInitList(br, t));
			if (!il.Ok()) return QueryResult<void>::Fail(il.error, il.message);
			region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(m_Ctx.out->NewNode<StoreNode>(target, m_Ctx.LoadIfNeeded(il.value))));
			return QueryResult<void>::Success();
		}

		// A type with creates is constructed by one (`Vector<int32> v = {1, 2, 3}` takes the init-list create).
		const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(t));
		if (obj && !obj->IsPurePackedVector() && !LifecycleFacts::Own(m_Ctx.queries(), obj).creates.empty())
		{
			if (br->HasDesignators())
				return m_Ctx.Fail(site, QueryResult<void>::Fail(QueryError::NotAValue, "designated entries name the fields of an aggregate, but '" +
					obj->GetName() + "' is constructed by a create: pass its arguments in order"));
			auto args = m_Ctx.Fail(site, ExprLowerer(m_Ctx).ConstructionArguments(br->GetElements()));
			if (!args.Ok()) return QueryResult<void>::Fail(args.error, args.message);
			return ConstructFrom(obj, args.value, true, target, region, site);
		}

		// A fill that throws destroys the parts it already filled (the rest are still zero).
		return FillAggregate(t, br, target, Lifecycle(m_Ctx).FillGuard(region, target), name);
	}

	QueryResult<void> DeclLowerer::FillAggregate(const Type* t, const BracedInitializerNode* br,
		const LValue* baseLoc, BlockNode* block, const Core::String& name)
	{
		if (const auto* obj = dynamic_cast<const ObjectType*>(t))
			return obj->IsPurePackedVector() ? FillLanes(obj->GetPackedVectorSwizzle(), br, baseLoc, block, name) : FillFields(obj, br, baseLoc, block, name);
		if (const auto* ar = dynamic_cast<const ArrayType*>(t)) return FillElements(ar, br, baseLoc, block, name);
		if (const auto* vector = dynamic_cast<const SwizzleType*>(t)) return FillLanes(vector, br, baseLoc, block, name);
		return QueryResult<void>::Fail(QueryError::NotAValue, "cannot aggregate-initialize '" + Core::String(t->GetName()) + "'");
	}

	QueryResult<void> DeclLowerer::StoreEntry(const LValue& target, const AstNode* valueNode, BlockNode* block, const Core::String& name)
	{
		// A nested brace fills the part in place; the part's lvalue is persisted first because the nested
		// fill's lvalues point at it.
		const ReferenceType* ref = ReferenceBinding::Of(target.type);
		if (const auto* nested = dynamic_cast<const BracedInitializerNode*>(valueNode))
		{
			if (ref)
				return m_Ctx.Fail(nested, QueryResult<void>::Fail(QueryError::NotAValue, "a braced list cannot bind the reference '" +
					Core::String(target.field ? target.field->mName : name) + "': a reference names a location"));
			return FillAggregate(target.type, nested, m_Ctx.NewLValue(target), block, name);
		}
		if (ref)
		{
			auto location = ExprLowerer(m_Ctx).ResolveExpr(valueNode);
			if (!location.Ok()) return QueryResult<void>::Fail(location.error, location.message);
			auto address = m_Ctx.Fail(valueNode, ReferenceBinding::Address(m_Ctx, ref, location.value, ReferenceBinding::PointerFor(m_Ctx, ref),
				target.field ? target.field->mName : name));
			if (!address.Ok()) return QueryResult<void>::Fail(address.error, address.message);
			block->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(ReferenceBinding::Bind(m_Ctx, m_Ctx.NewLValue(target), address.value)));
			return QueryResult<void>::Success();
		}
		auto v = ExprLowerer(m_Ctx).ResolveFor(valueNode, target.type);
		if (!v.Ok()) return QueryResult<void>::Fail(v.error, v.message);
		auto initial = InitialValue(m_Ctx, v.value, target.type);
		if (!initial.Ok()) return QueryResult<void>::Fail(initial.error, initial.message);
		ImplicitConversion::WarnIfNarrowing(m_Ctx, valueNode, v.value, target.type);
		const HIRNode* val = initial.value;
		if (val->type != target.type) val = m_Ctx.out->NewNode<ConvertNode>(val, val->type, target.type);
		block->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(m_Ctx.out->NewNode<StoreNode>(m_Ctx.NewLValue(target), val)));
		return QueryResult<void>::Success();
	}

	QueryResult<std::vector<DeclLowerer::BracedItem>> DeclLowerer::BracedItems(const BracedInitializerNode* br)
	{
		using Result = QueryResult<std::vector<BracedItem>>;
		std::vector<BracedItem> items;
		for (const auto& e : br->GetEntries())
		{
			const auto* expansion = dynamic_cast<const PackExpansionNode*>(e.m_Value.Get());
			if (!expansion)
			{
				items.push_back({ &e, nullptr, 0 });
				continue;
			}
			if (e.m_Key)
				return m_Ctx.Fail(expansion, Result::Fail(QueryError::NotAValue, "a designated entry names one part: it cannot expand a pack"));
			auto copies = PackElements::Of(m_Ctx, *expansion->GetPattern());
			if (!copies.Ok()) return Result::Fail(copies.error, copies.message);
			auto shared = std::make_shared<const PackElements>(std::move(copies.value));
			for (uint32 i = 0; i < shared->Count(); ++i) items.push_back({ &e, shared, i });
		}
		return Result(items);
	}

	QueryResult<void> DeclLowerer::StoreItem(const LValue& target, const BracedItem& item, BlockNode* block, const Core::String& name)
	{
		auto store = [&] { return StoreEntry(target, item.Value(), block, name); };
		return item.copies ? item.copies->Within(item.copy, store) : store();
	}

	QueryResult<void> DeclLowerer::FillLanes(const SwizzleType* swz, const BracedInitializerNode* br, const LValue* baseLoc,
		BlockNode* block, const Core::String& name)
	{
		// A packed vector (a `packed T[N]` or a pure packed vector object) fills its SIMD lanes in order;
		// lanes no entry names stay zero.
		const uint32 laneCount = swz ? swz->GetLaneCount() : 0;
		auto items = BracedItems(br);
		if (!items.Ok()) return QueryResult<void>::Fail(items.error, items.message);
		for (uint32 i = 0; i < items.value.size(); ++i)
		{
			if (i >= laneCount) return QueryResult<void>::Fail(QueryError::NotAValue, "too many initializers for '" + name + "'");
			LValue lv; lv.kind = LValue::Kind::Lane; lv.type = swz->GetElementType(); lv.base = baseLoc; lv.laneIndex = i;
			if (auto r = StoreItem(lv, items.value[i], block, name); !r.Ok()) return r;
		}
		return QueryResult<void>::Success();
	}

	QueryResult<void> DeclLowerer::FillFields(const ObjectType* obj, const BracedInitializerNode* br, const LValue* baseLoc,
		BlockNode* block, const Core::String& name)
	{
		// Positional entries fill the aggregate's slots in order (AggregateSlots); a bare-name key names any field or lane.
		const std::vector<AggregateSlots::Slot> slots = AggregateSlots::Of(*obj);
		uint32 nextSlot = 0;
		std::vector<std::pair<size_t, int32>> named; // offset and lane of every slot an entry filled
		std::vector<const ObjectType::FieldInfo*> given;
		auto items = BracedItems(br);
		if (!items.Ok()) return QueryResult<void>::Fail(items.error, items.message);
		for (const BracedItem& item : items.value)
		{
			const BracedInitializerNode::Entry& e = *item.entry;
			AggregateSlots::Slot slot;
			if (!e.m_Key)
			{
				if (nextSlot >= slots.size())
					return QueryResult<void>::Fail(QueryError::NotAValue, "too many initializers for '" + name + "'");
				slot = slots[nextSlot++];
			}
			else if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(e.m_Key.Get()))
			{
				if (!AggregateSlots::Named(*obj, id->GetName(), slot))
					return QueryResult<void>::Fail(QueryError::NotFound, "no field '" + id->GetName() + "' in '" + name + "'");
			}
			else
				return QueryResult<void>::Fail(QueryError::NotAValue, "array-style key is not valid on struct '" + name + "'");
			if (std::find(named.begin(), named.end(), std::make_pair(slot.offset, slot.lane)) != named.end())
				return QueryResult<void>::Fail(QueryError::NotAValue, "field '" + slot.name + "' of '" + name + "' is initialized twice");
			if (auto r = StoreItem(*SlotLValue(slot, baseLoc), item, block, name); !r.Ok()) return r;
			named.push_back({ slot.offset, slot.lane });
			given.push_back(slot.field);
		}
		return FillOmittedFields(obj, given, baseLoc, block);
	}

	QueryResult<uint64> DeclLowerer::ElementsNamed(const BracedInitializerNode* br, const Core::String& name)
	{
		auto entries = ElementEntries(UINT64_MAX, br, name);
		if (!entries.Ok()) return QueryResult<uint64>::Fail(entries.error, entries.message);
		return QueryResult<uint64>(entries.value.empty() ? 0 : entries.value.back().last + 1);
	}

	QueryResult<std::vector<DeclLowerer::ElementEntry>> DeclLowerer::ElementEntries(uint64 count, const BracedInitializerNode* br,
		const Core::String& name)
	{
		// Where each entry lands: a positional one continues after the previous entry, so { 1, 2, [5] = 9, 7 }
		// puts 7 at [6]. The result is in index order; no element may be named twice.
		using Result = QueryResult<std::vector<ElementEntry>>;
		ConstEvaluator eval(*m_Ctx.queries().Model(), m_Ctx.scope);
		auto foldIndex = [&](const AstNode* key, uint64* out) -> QueryResult<void> {
			auto v = eval.Evaluate(key, m_Ctx.unitId);
			if (!v.Ok()) return QueryResult<void>::Fail(QueryError::NotAValue, "array initializer index is not a constant: " + v.message);
			*out = (uint64)v.value.AsInt();
			return QueryResult<void>::Success();
		};
		auto items = BracedItems(br);
		if (!items.Ok()) return Result::Fail(items.error, items.message);
		std::vector<ElementEntry> entries;
		uint64 next = 0;
		for (const BracedItem& item : items.value)
		{
			const BracedInitializerNode::Entry& e = *item.entry;
			ElementEntry entry{ next, next, item };
			if (e.m_Key)
			{
				if (auto r = foldIndex(e.m_Key.Get(), &entry.first); !r.Ok()) return Result::Fail(r.error, r.message);
				entry.last = entry.first;
				if (e.m_IsRange)
					if (auto r = foldIndex(e.m_RangeEnd.Get(), &entry.last); !r.Ok()) return Result::Fail(r.error, r.message);
				if (entry.last < entry.first)
					return Result::Fail(QueryError::NotAValue, "the range [" + std::to_string(entry.first) + "..." + std::to_string(entry.last) + "] of '" + name + "' is empty");
			}
			if (entry.last >= count)
				return Result::Fail(QueryError::NotAValue, e.m_Key ? "array initializer index out of range for '" + name + "'" : "too many initializers for '" + name + "'");
			entries.push_back(entry);
			next = entry.last + 1;
		}
		std::stable_sort(entries.begin(), entries.end(), [](const ElementEntry& a, const ElementEntry& b) { return a.first < b.first; });
		for (size_t i = 1; i < entries.size(); ++i)
			if (entries[i].first <= entries[i - 1].last)
				return Result::Fail(QueryError::NotAValue, "element " + std::to_string(entries[i].first) + " of '" + name + "' is initialized twice");
		return Result(entries);
	}

	QueryResult<void> DeclLowerer::FillElements(const ArrayType* ar, const BracedInitializerNode* br, const LValue* baseLoc,
		BlockNode* block, const Core::String& name)
	{
		// Elements start in index order; one no entry names is default-constructed. A range entry's value is
		// evaluated once, into its first element, and copied into the rest.
		auto entries = ElementEntries(ar->GetElementCount(), br, name);
		if (!entries.Ok()) return QueryResult<void>::Fail(entries.error, entries.message);
		const Type* elemType = ar->GetElementType();
		Value array; array.isLValue = true; array.lvalue = baseLoc; array.type = ar;
		Lifecycle life(m_Ctx);
		auto defaultElements = [&](uint64 begin, uint64 end) -> QueryResult<void> {
			if (begin >= end) return QueryResult<void>::Success();
			if (auto allowed = AccessCheck::CheckDefaultCreate(m_Ctx, elemType); !allowed.Ok()) return allowed;
			life.ConstructElements(block, baseLoc, begin, end);
			return QueryResult<void>::Success();
		};
		uint64 next = 0;
		for (const ElementEntry& e : entries.value)
		{
			if (auto r = defaultElements(next, e.first); !r.Ok()) return r;
			const HIRNode* index = m_Ctx.out->NewNode<ConstantNode>(m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64), ConstantValue::Int((int64)e.first));
			if (auto r = StoreItem(*BuildIndexLValue(m_Ctx, array, index, elemType, elemType->GetSize()), e.item, block, name); !r.Ok()) return r;
			if (e.last > e.first)
			{
				if (auto copyable = LifecycleFacts::CheckCopyable(m_Ctx.queries(), elemType); !copyable.Ok())
					return QueryResult<void>::Fail(copyable.error, "a range entry copies its value into each element: " + copyable.message);
				life.CopyElement(block, baseLoc, e.first, e.first + 1, e.last + 1);
			}
			next = e.last + 1;
		}
		return defaultElements(next, ar->GetElementCount());
	}

	const LValue* DeclLowerer::SlotLValue(const AggregateSlots::Slot& slot, const LValue* object)
	{
		LValue field; field.kind = LValue::Kind::Field; field.type = slot.field->m_Type; field.base = object; field.field = slot.field;
		if (slot.lane < 0) return m_Ctx.NewLValue(field);
		LValue lane; lane.kind = LValue::Kind::Lane; lane.type = slot.type; lane.base = m_Ctx.NewLValue(field); lane.laneIndex = (uint32)slot.lane;
		return m_Ctx.NewLValue(lane);
	}

	QueryResult<void> DeclLowerer::FillOmittedFields(const ObjectType* obj, const std::vector<const ObjectType::FieldInfo*>& given,
		const LValue* baseLoc, BlockNode* block)
	{
		// A field no entry names starts from its initializer (evaluated without an object, as in `T{...}`),
		// else from its default construction.
		QueryResult<void> status = QueryResult<void>::Success();
		LifecycleFacts::ForEachField(obj, [&](const ObjectType::FieldInfo& f) {
			if (!status.Ok() || std::find(given.begin(), given.end(), &f) != given.end()) return;
			LValue lv; lv.kind = LValue::Kind::Field; lv.type = f.m_Type; lv.base = baseLoc; lv.field = &f;
			const LValue* field = m_Ctx.NewLValue(lv);
			if (!f.m_Initializer && ReferenceBinding::Of(f.m_Type))
			{
				status = QueryResult<void>::Fail(QueryError::NotAValue, ReferenceBinding::Unbound(obj, f));
				return;
			}
			if (!f.m_Initializer)
			{
				status = AccessCheck::CheckDefaultCreate(m_Ctx, f.m_Type);
				if (status.Ok()) Lifecycle(m_Ctx).Construct(block, field);
				return;
			}
			auto value = OmittedFieldValue(m_Ctx, obj, f);
			if (!value.Ok()) { status = QueryResult<void>::Fail(value.error, value.message); return; }
			block->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(m_Ctx.out->NewNode<StoreNode>(field, value.value)));
		});
		return status;
	}
} // namespace Scripting
