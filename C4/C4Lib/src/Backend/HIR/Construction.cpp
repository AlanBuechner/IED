#include "Backend/HIR/Construction.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/AccessCheck.h"
#include "Model/Abstractness.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/Lifecycle.h"
#include "Model/OverloadResolution.h"
#include "Model/Types/AggregateSlots.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h"
#include "Model/TypeHandle.h"
#include "Model/Types/InitListType.h"
#include "Model/Types/SwizzleType.h"

namespace Scripting
{
	namespace
	{
		// A braced list builds no object before its elements, so an omitted field's initializer is evaluated
		// without one: in the type's scope and in a frame of its own (constants, globals and static members are
		// visible; other fields, `this` and the caller's locals are not).
		QueryResult<const HIRNode*> InitializerOutsideObject(LoweringContext& ctx, const ObjectType* type, const ObjectType::FieldInfo& field)
		{
			const ObjectType* enclosing = ctx.enclosingType;
			const LValue* capturedThis = ctx.capturedThis;
			const LValue* closureObject = ctx.closureObject;
			const Scope* scope = ctx.scope;
			ctx.enclosingType = nullptr;
			ctx.capturedThis = nullptr;
			ctx.closureObject = nullptr;
			if (const Scope* own = ctx.model().TypeScope(type)) ctx.scope = own;
			QueryResult<const HIRNode*> result = QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, "");
			{
				ScopeGuard frame = ctx.queries().PushScope(Scope::Kind::Function, "__field_initializer");
				LoweringContext::FailureNote why(ctx, " (in the initializer of '" + field.mName + "', which a braced list evaluates without an object)");
				auto value = ExprLowerer(ctx).ResolveFor(field.m_Initializer, field.m_Type);
				result = value.Ok() ? ctx.Fail(field.m_Initializer, InitialValue(ctx, value.value, field.m_Type))
					: QueryResult<const HIRNode*>::Fail(value.error, value.message);
			}
			ctx.enclosingType = enclosing;
			ctx.capturedThis = capturedThis;
			ctx.closureObject = closureObject;
			ctx.scope = scope;
			return result;
		}
	}

	QueryResult<const HIRNode*> OmittedFieldValue(LoweringContext& ctx, const ObjectType* type, const ObjectType::FieldInfo& field)
	{
		if (field.m_Initializer) return InitializerOutsideObject(ctx, type, field);
		if (ReferenceBinding::Of(field.m_Type)) return QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, ReferenceBinding::Unbound(type, field));
		const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(field.m_Type));
		if (auto allowed = AccessCheck::CheckDefaultCreate(ctx, field.m_Type); !allowed.Ok())
			return QueryResult<const HIRNode*>::Fail(allowed.error, allowed.message);
		if (obj && LifecycleFacts::NeedsConstruct(ctx.queries(), obj))
		{
			auto* construct = ctx.out->NewNode<ConstructNode>(obj);
			construct->ctor = Lifecycle(ctx).DefaultCreate(obj);
			construct->args = Lifecycle(ctx).DefaultArguments(construct->ctor);
			return QueryResult<const HIRNode*>(construct);
		}
		if (LifecycleFacts::NeedsConstruct(ctx.queries(), field.m_Type))
			return QueryResult<const HIRNode*>(Lifecycle(ctx).DefaultValue(field.m_Type));
		return QueryResult<const HIRNode*>(ctx.out->NewNode<ConstantNode>(field.m_Type, ConstantValue()));
	}

	namespace
	{
		using Plan = QueryResult<ConstructionPlan>;
		using Args = std::vector<ConstructionArgument>;

		// The value an argument gives a parameter or field of type `target`.
		QueryResult<const HIRNode*> ArgumentValue(LoweringContext& ctx, ExprLowerer& expr, const ConstructionArgument& a, const Type* target)
		{
			if (!a.brace) return InitialValue(ctx, a.value, target);
			auto v = expr.ResolveBracedAs(a.brace, target);
			if (!v.Ok()) return QueryResult<const HIRNode*>::Fail(v.error, v.message);
			return QueryResult<const HIRNode*>(ctx.LoadIfNeeded(v.value));
		}

		const FunctionRecord* InitListCreate(const std::vector<const FunctionRecord*>& creates, const Type** listType)
		{
			for (const FunctionRecord* rec : creates)
			{
				if (!rec->type || rec->type->GetParameterCount() != 1) continue;
				const Type* p = TypeHandle::UnwrapToValue(rec->type->GetParameters()[0].m_Type);
				if (p && p->GetKind() == TypeKind::InitList) { *listType = p; return rec; }
			}
			return nullptr;
		}

		Plan ListPlan(LoweringContext& ctx, ExprLowerer& expr, const FunctionRecord* create, const Type* listType, const Args& args)
		{
			const Type* elemT = static_cast<const InitListType*>(listType)->GetElementType();
			auto* list = ctx.out->NewNode<InitListNode>(listType);
			for (const ConstructionArgument& a : args)
			{
				auto element = ArgumentValue(ctx, expr, a, elemT);
				if (!element.Ok()) return Plan::Fail(element.error, element.message);
				list->elements.push_back(element.value);
			}
			ConstructionPlan plan;
			plan.create = create;
			plan.args.push_back(list);
			return plan;
		}

		// The fields an aggregate's arguments fill, in order (the codegen fill walks the same list).
		// A slot no argument fills: a field's initializer or default construction; a lane stays zero.
		QueryResult<const HIRNode*> OmittedSlotValue(LoweringContext& ctx, const ObjectType* type, const AggregateSlots::Slot& slot)
		{
			if (slot.lane < 0) return OmittedFieldValue(ctx, type, *slot.field);
			return QueryResult<const HIRNode*>(ctx.out->NewNode<ConstantNode>(slot.type, ConstantValue::Int(0)));
		}

		bool SlotNeedsValue(LoweringContext& ctx, const AggregateSlots::Slot& slot)
		{
			return slot.lane < 0 && (slot.field->m_Initializer || ReferenceBinding::Of(slot.field->m_Type) ||
				LifecycleFacts::NeedsConstruct(ctx.queries(), slot.field->m_Type));
		}

		// The arguments fill the aggregate's slots in order (AggregateSlots: one view of each union, packed lanes).
		Plan AggregatePlan(LoweringContext& ctx, ExprLowerer& expr, const ObjectType* type, const Args& args)
		{
			const std::vector<AggregateSlots::Slot> slots = AggregateSlots::Of(*type);
			if (args.size() > slots.size())
				return Plan::Fail(QueryError::NotAValue, "too many initializers for '" + type->GetName() + "'");
			size_t filled = args.size(); // slots up to the last one that needs construction or has an initializer get a value
			for (size_t i = args.size(); i < slots.size(); ++i)
				if (SlotNeedsValue(ctx, slots[i])) filled = i + 1;
			ConstructionPlan plan;
			plan.aggregate = true;
			for (size_t i = 0; i < filled; ++i)
			{
				auto value = i < args.size() ? ArgumentValue(ctx, expr, args[i], slots[i].type) : OmittedSlotValue(ctx, type, slots[i]);
				if (!value.Ok()) return Plan::Fail(value.error, value.message);
				plan.args.push_back(value.value);
			}
			// When a field needs destruction, every argument is computed before the first field is filled: one
			// that throws then finds the earlier ones in temporaries, which are destroyed, not in a half-built object.
			bool anyOwned = false;
			for (size_t i = 0; i < filled; ++i) anyOwned = anyOwned || LifecycleFacts::NeedsDestroy(ctx.queries(), slots[i].type);
			if (anyOwned)
				for (const HIRNode*& arg : plan.args) arg = Lifecycle(ctx).Hold(arg, plan.prelude);
			return plan;
		}

		// A pure packed vector has no ordinary fields: the arguments are its SIMD lanes in order.
		Plan LanePlan(LoweringContext& ctx, ExprLowerer& expr, const ObjectType* type, const Args& args)
		{
			const SwizzleType* lanes = type->GetPackedVectorSwizzle();
			if (!lanes || args.size() > lanes->GetLaneCount())
				return Plan::Fail(QueryError::NotAValue, "too many initializers for '" + type->GetName() + "'");
			ConstructionPlan plan;
			plan.aggregate = true;
			for (const ConstructionArgument& a : args)
			{
				auto lane = ArgumentValue(ctx, expr, a, lanes->GetElementType());
				if (!lane.Ok()) return Plan::Fail(lane.error, lane.message);
				plan.args.push_back(lane.value);
			}
			return plan;
		}

		bool IsSameTypeValue(const Args& args, const ObjectType* type)
		{
			return args.size() == 1 && !args[0].brace && TypeHandle::UnwrapToValue(args[0].value.type) == type;
		}

		bool SomeCreateTakesOwnType(const std::vector<const FunctionRecord*>& creates, const ObjectType* type)
		{
			for (const FunctionRecord* rec : creates)
				if (rec->type && rec->type->GetParameterCount() == 1 &&
					TypeHandle::UnwrapToValue(rec->type->GetParameters()[0].m_Type) == type) return true;
			return false;
		}

		Plan WholePlan(LoweringContext& ctx, const ObjectType* type, const Value& source)
		{
			auto value = InitialValue(ctx, source, type);
			if (!value.Ok()) return Plan::Fail(value.error, value.message);
			ConstructionPlan plan;
			plan.whole = value.value;
			return plan;
		}

		Plan CallPlan(LoweringContext& ctx, ExprLowerer& expr, const FunctionRecord* create, const Args& args)
		{
			ConstructionPlan plan;
			plan.create = create;
			const auto& params = create->type->GetParameters();
			for (size_t i = 0; i < params.Count(); ++i)
			{
				Value arg;
				if (i < args.size() && args[i].brace)
				{
					auto v = ArgumentValue(ctx, expr, args[i], TypeHandle::UnwrapToValue(params[(uint32)i].m_Type));
					if (!v.Ok()) return Plan::Fail(v.error, v.message);
					plan.args.push_back(v.value);
					continue;
				}
				if (i < args.size()) arg = args[i].value;
				else
				{
					if (i >= create->paramDefaults.Count() || !create->paramDefaults[(uint32)i])
						return Plan::Fail(QueryError::NotFound, "too few arguments for the create '" + create->name + "'");
					auto dv = expr.DefaultArgument(*create, (uint32)i);
					if (!dv.Ok()) return Plan::Fail(dv.error, dv.message);
					arg = dv.value;
				}
				auto bound = BindArgument(ctx, arg, ctx.LoadIfNeeded(arg), params[(uint32)i].m_Type);
				if (!bound.Ok()) return Plan::Fail(bound.error, bound.message);
				plan.args.push_back(bound.value);
			}
			return plan;
		}
	}

	static QueryResult<ConstructionPlan> ChooseConstruction(LoweringContext& ctx, ExprLowerer& expr, const ObjectType* type,
		const std::vector<ConstructionArgument>& args, bool braced, const AstNode* site)
	{
		LifecycleFacts::OwnMethods own = LifecycleFacts::Own(ctx.queries(), type);
		const Type* listType = nullptr;
		if (braced)
			if (const FunctionRecord* listCreate = InitListCreate(own.creates, &listType))
				return ctx.Fail(site, ListPlan(ctx, expr, listCreate, listType, args));
		if (args.empty())
		{
			ConstructionPlan plan;
			plan.create = Lifecycle(ctx).DefaultCreate(type);
			plan.args = Lifecycle(ctx).DefaultArguments(plan.create);
			return plan;
		}
		if (IsSameTypeValue(args, type) && !SomeCreateTakesOwnType(own.creates, type))
			return ctx.Fail(site, WholePlan(ctx, type, args[0].value));
		if (own.creates.empty() && type->IsPurePackedVector()) return ctx.Fail(site, LanePlan(ctx, expr, type, args));
		if (own.creates.empty()) return ctx.Fail(site, AggregatePlan(ctx, expr, type, args));

		Core::Vector<OverloadCandidate> candidates;
		for (const FunctionRecord* rec : own.creates) candidates.Push({ rec->type, rec, rec->minArity });
		Core::Vector<const Type*> argTypes;
		for (const ConstructionArgument& a : args)
			argTypes.Push(a.brace ? ctx.queries().TypeStore()->BracedList(a.brace->EntryCount()) : TypeHandle::UnwrapToValue(a.value.type));
		OverloadResolution resolved = OverloadResolver::Resolve(candidates, argTypes);
		if (resolved.status != OverloadResolution::Status::Resolved || !resolved.chosenRecord)
			return ctx.Fail(site, Plan::Fail(QueryError::NotFound, "no matching constructor of '" + type->GetName() + "' with " +
				std::to_string(args.size()) + " argument(s)"));
		return ctx.Fail(site, CallPlan(ctx, expr, resolved.chosenRecord, args));
	}

	QueryResult<ConstructionPlan> PlanConstruction(LoweringContext& ctx, ExprLowerer& expr, const ObjectType* type,
		const std::vector<ConstructionArgument>& args, bool braced, const AstNode* site)
	{
		if (Core::String why = Abstractness::Creation(ctx.model(), type); !why.empty())
			return ctx.Fail(site, Plan::Fail(QueryError::NotAValue, why));
		Plan plan = ChooseConstruction(ctx, expr, type, args, braced, site);
		if (!plan.Ok() || !plan.value.create) return plan;
		auto allowed = AccessCheck::CheckCall(ctx, *plan.value.create);
		return allowed.Ok() ? plan : ctx.Fail(site, Plan::Fail(allowed.error, allowed.message));
	}
} // namespace Scripting
