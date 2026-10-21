#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"

namespace Scripting
{
	QueryResult<void> Lifecycle::LowerSynthesized(const FunctionRecord* rec, BlockNode* root)
	{
		const ObjectType* type = rec->containingType;
		switch (rec->synthesized)
		{
		case LifecycleOp::Create:
			return ConstructParts(PartsGuard(root, type), type, false, nullptr);
		case LifecycleOp::Delete:
			ScheduleDestroyParts(root, type);
			return QueryResult<void>::Success();
		case LifecycleOp::Copy:
		{
			// `other` is parameter 0, a reference: its slot holds the source's address.
			LValue slotLv; slotLv.kind = LValue::Kind::Slot; slotLv.slot = 0; slotLv.type = rec->type->GetParameters()[0].m_Type;
			LValue otherLv; otherLv.kind = LValue::Kind::Deref; otherLv.type = type;
			otherLv.addr = m_Ctx.out->NewNode<LoadNode>(m_Ctx.queries().TypeStore()->Pointer(type, true), m_Ctx.NewLValue(slotLv));
			const LValue* other = m_Ctx.NewLValue(otherLv);
			BlockNode* body = PartsGuard(root, type);
			CopyBase(body, type, other);
			LifecycleFacts::ForEachField(type, [&](const ObjectType::FieldInfo& f) { CopyInto(body, FieldOf(This(type), f), FieldOf(other, f)); });
			return QueryResult<void>::Success();
		}
		default:
			return QueryResult<void>::Success();
		}
	}

	QueryResult<void> Lifecycle::ConstructParts(BlockNode* root, const ObjectType* type, bool baseHandledByBody, const AstNode* body)
	{
		const AstNode* site = body ? body : type->GetDeclNode(); // a compiler-written create reports at the type
		const LValue* self = This(type);
		if (const ObjectType* base = LifecycleFacts::ClassBase(type); base && !baseHandledByBody)
		{
			// A create the user writes must pick a base create with arguments; a compiler-written one builds the base
			// the way `Base b;` does.
			LifecycleFacts::OwnMethods own = LifecycleFacts::Own(m_Ctx.queries(), base);
			if (body && !own.creates.empty() && !own.defaultCreate)
				return m_Ctx.Fail(site, QueryResult<void>::Fail(QueryError::NotFound, "'" + base->GetName() +
					"' has no create that takes no arguments; call one as the first statement of the create of '" + type->GetName() + "'"));
			if (auto allowed = AccessCheck::CheckDefaultCreate(m_Ctx, base); !allowed.Ok())
				return m_Ctx.Fail(site, allowed);
			Construct(root, BaseOf(self, type, base));
		}
		QueryResult<void> status = QueryResult<void>::Success();
		LifecycleFacts::ForEachField(type, [&](const ObjectType::FieldInfo& f) {
			if (!status.Ok()) return;
			if (f.m_Initializer) status = InitializeField(root, FieldOf(self, f), f);
			else status = ConstructField(root, FieldOf(self, f), f, site);
		});
		return status;
	}

	// A field with no initializer is default-constructed; the create it runs must be one the type may call.
	QueryResult<void> Lifecycle::ConstructField(BlockNode* root, const LValue* field, const ObjectType::FieldInfo& info, const AstNode* site)
	{
		if (auto allowed = AccessCheck::CheckDefaultCreate(m_Ctx, info.m_Type); !allowed.Ok())
			return m_Ctx.Fail(info.m_DeclaringNode ? info.m_DeclaringNode : site, allowed);
		Construct(root, field);
		return QueryResult<void>::Success();
	}

	QueryResult<void> Lifecycle::InitializeField(BlockNode* root, const LValue* field, const ObjectType::FieldInfo& info)
	{
		// The field starts from its initializer, evaluated in the create before its body (C++ order).
		const size_t mark = m_Ctx.pendingTemporaries.size();
		auto value = ExprLowerer(m_Ctx).ResolveFor(info.m_Initializer, info.m_Type);
		if (!value.Ok()) return m_Ctx.Fail(info.m_Initializer, QueryResult<void>::Fail(value.error, value.message));
		auto initial = InitialValue(m_Ctx, value.value, info.m_Type);
		if (!initial.Ok()) return m_Ctx.Fail(info.m_Initializer, QueryResult<void>::Fail(initial.error, initial.message));
		root->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(field, EndFullExpression(initial.value, mark)));
		return QueryResult<void>::Success();
	}

	void Lifecycle::ScheduleDestroyParts(BlockNode* root, const ObjectType* type)
	{
		// Cleanups run last-registered first: the base is registered first so it is destroyed last, after
		// the fields in reverse declaration order, after the delete body.
		const LValue* self = This(type);
		if (const ObjectType* base = LifecycleFacts::ClassBase(type)) ScheduleDestroy(root, BaseOf(self, type, base));
		LifecycleFacts::ForEachField(type, [&](const ObjectType::FieldInfo& f) { ScheduleDestroy(root, FieldOf(self, f)); });
	}

	BlockNode* Lifecycle::PartsGuard(BlockNode* root, const ObjectType* type)
	{
		if (!PartsNeedDestroy(type)) return root;
		const LValue* self = This(type);
		return GuardWith(root, [&](BlockNode* undo) { DestroyPartsNow(undo, self, type); });
	}

	BlockNode* Lifecycle::FillGuard(BlockNode* root, const LValue* object)
	{
		if (const ObjectType* obj = AsObject(object->type))
		{
			if (!PartsNeedDestroy(obj)) return root;
			return GuardWith(root, [&](BlockNode* undo) { DestroyPartsNow(undo, object, obj); });
		}
		if (!AsArray(object->type) || !LifecycleFacts::NeedsDestroy(m_Ctx.queries(), object->type)) return root;
		return GuardWith(root, [&](BlockNode* undo) { DestroyNow(undo, object); });
	}

	bool Lifecycle::PartsNeedDestroy(const ObjectType* type)
	{
		const ObjectType* base = LifecycleFacts::ClassBase(type);
		bool needed = base && LifecycleFacts::NeedsDestroy(m_Ctx.queries(), base);
		LifecycleFacts::ForEachField(type, [&](const ObjectType::FieldInfo& f) {
			needed = needed || LifecycleFacts::NeedsDestroy(m_Ctx.queries(), f.m_Type);
		});
		return needed;
	}

	BlockNode* Lifecycle::GuardWith(BlockNode* root, const std::function<void(BlockNode*)>& undo)
	{
		// A block whose first statement is a cleanup that only an unwinding exception runs.
		auto* guarded = m_Ctx.out->NewNode<BlockNode>(nullptr);
		auto* cleanup = m_Ctx.out->NewNode<DeferNode>();
		cleanup->body = m_Ctx.out->NewNode<BlockNode>(nullptr);
		cleanup->exceptionalOnly = true;
		undo(cleanup->body);
		guarded->stmts.push_back(cleanup);
		root->stmts.push_back(guarded);
		return guarded;
	}

	void Lifecycle::DestroyPartsNow(BlockNode* region, const LValue* object, const ObjectType* type)
	{
		std::vector<const ObjectType::FieldInfo*> fields;
		LifecycleFacts::ForEachField(type, [&](const ObjectType::FieldInfo& f) { fields.push_back(&f); });
		for (auto it = fields.rbegin(); it != fields.rend(); ++it) DestroyNow(region, FieldOf(object, **it));
		if (const ObjectType* base = LifecycleFacts::ClassBase(type)) DestroyNow(region, BaseOf(object, type, base));
	}
} // namespace Scripting
