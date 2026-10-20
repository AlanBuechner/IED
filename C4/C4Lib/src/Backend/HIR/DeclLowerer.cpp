#include "Backend/HIR/DeclLowerer.h"
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
#include "Model/ConstConversions.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/SwizzleType.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		const ReferenceType* AsReference(const Type* t)
		{
			return t->GetKind() == TypeKind::Reference ? static_cast<const ReferenceType*>(t) : nullptr;
		}

		bool StorageCanBePartlyWritten(const Type* t)
		{
			const TypeKind kind = TypeHandle::UnwrapToValue(t)->GetKind();
			return kind == TypeKind::Object || kind == TypeKind::Array;
		}

		// `T v(a)` for a T that is not an object starts v from `a`, as `T v = a;` does; null when there are no arguments
		// or T is an object, which its create starts. Such a T takes one value, not several.
		QueryResult<const AstNode*> ValueArgument(const Core::Vector<Ref<AstNode>>& arguments, const Type* t, const Core::String& name)
		{
			const bool object = !AsReference(t) && TypeHandle::UnwrapToValue(t)->GetKind() == TypeKind::Object;
			if (object || arguments.Empty()) return QueryResult<const AstNode*>::Success(nullptr);
			if (arguments.Count() == 1) return QueryResult<const AstNode*>::Success(arguments[0].Get());
			return QueryResult<const AstNode*>::Fail(QueryError::NotCallable,
				"'" + name + "' is not an object: it starts from one value, not " + std::to_string(arguments.Count()));
		}

		VariableStart StartOf(const VariableDefinitionNode* n, const Declarator& d)
		{
			VariableStart start;
			start.initializer = d.m_Initializer ? d.m_Initializer.Get() : nullptr;
			start.arguments = &d.m_CtorArgs;
			start.site = n;
			start.name = d.m_NameToken.GetText();
			return start;
		}
	}

	// --- declarations ------------------------------------------------------------------------------------

	QueryResult<void> DeclLowerer::LowerLocalDecl(const VariableDefinitionNode* n, BlockNode* region)
	{
		auto typeR = m_Ctx.Fail(n, m_Ctx.queries().ResolveTypeCtx(*n->GetType().Get(), m_Ctx.unitId, m_Ctx.scope));
		if (!typeR.Ok()) return QueryResult<void>::Fail(typeR.error, typeR.message);
		const Type* t = typeR.value;
		if (Core::String why = Abstractness::ByValue(m_Ctx.model(), "'" + n->GetNameToken().GetText() + "'", t); !why.empty())
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, why));

		// A thread_local local has static storage, as in C++.
		if (n->IsStatic() || n->IsThreadLocal())
		{
			for (const Declarator& d : n->GetDeclarators())
				if (auto r = LowerStaticLocal(n, d, t, region); !r.Ok()) return r;
			return QueryResult<void>::Success();
		}

		// A single declarator with a braced aggregate initializer is filled in place.
		if (n->DeclaratorCount() == 1)
			if (const auto* br = dynamic_cast<const BracedInitializerNode*>(n->GetInitializer().Get()))
				return LowerAggregateInit(n, t, br, region);

		// Every declarator in the comma list gets its own slot. C semantics: each initializer is evaluated
		// BEFORE its own name enters scope (so `int x = x;` fails) but AFTER earlier declarators are
		// registered (so `int a = 1, b = a;` works).
		for (const Declarator& d : n->GetDeclarators())
			if (auto r = LowerLocal(n, d, t, region); !r.Ok()) return r;
		return QueryResult<void>::Success();
	}

	QueryResult<void> DeclLowerer::LowerLocal(const VariableDefinitionNode* n, const Declarator& d, const Type* t, BlockNode* region)
	{
		const Core::String name = d.m_NameToken.GetText();
		const ReferenceType* ref = AsReference(t);
		auto argument = m_Ctx.Fail(n, ValueArgument(d.m_CtorArgs, t, name));
		if (!argument.Ok()) return QueryResult<void>::Fail(argument.error, argument.message);
		const AstNode* initializer = d.m_Initializer ? d.m_Initializer.Get() : argument.value;

		// A value local starts as a copy of an lvalue or takes a fresh object (docs/language/06); a reference
		// local binds to the location instead.
		const HIRNode* initNode = nullptr;
		QueryResult<Value> initR;
		if (initializer)
		{
			initR = m_Ctx.Fail(n, ExprLowerer(m_Ctx).ResolveFor(initializer, t));
			if (!initR.Ok()) return QueryResult<void>::Fail(initR.error, initR.message);
			if (!ref)
			{
				auto initial = m_Ctx.Fail(n, InitialValue(m_Ctx, initR.value, t));
				if (!initial.Ok()) return QueryResult<void>::Fail(initial.error, initial.message);
				ImplicitConversion::WarnIfNarrowing(m_Ctx, initializer, initR.value, t);
				initNode = initial.value;
			}
		}
		const Core::Vector<Ref<AstNode>> none;
		auto ctorArgs = m_Ctx.Fail(n, ExprLowerer(m_Ctx).ConstructionArguments(argument.value ? none : d.m_CtorArgs));
		if (!ctorArgs.Ok()) return QueryResult<void>::Fail(ctorArgs.error, ctorArgs.message);

		// A reference local (`Counter& c = WholeRef();`) is an ALIAS to a location: the slot holds the
		// referent's ADDRESS and reads auto-deref, exactly like a reference parameter.
		const Type* slotType = ref ? m_Ctx.queries().TypeStore()->Pointer(TypeHandle::UnwrapToValue(ref->GetReferentType())) : t;
		if (ref)
		{
			auto address = ReferenceAddress(ref, initializer ? &initR.value : nullptr, slotType, name);
			if (!address.Ok()) return m_Ctx.Fail(n, QueryResult<void>::Fail(address.error, address.message));
			initNode = address.value;
		}

		const uint32 slot = m_Ctx.AllocSlot(slotType, name, SlotClass::Local);
		m_Ctx.out->SetStorageModifiers(slot, n->IsVolatile(), false);
		if (auto bound = BindName(n, name, slotType, slot, ref); !bound.Ok()) return bound;
		m_Ctx.jumps.NoteDeclaration("the declaration of '" + name + "'");
		auto decl = m_Ctx.out->NewNode<LocalDeclNode>(slot, slotType, initNode);
		decl->m_Name = name;
		region->stmts.push_back(decl);
		if (ref) return QueryResult<void>::Success();
		return LowerValueLocalLifetime(n, TypeHandle::UnwrapToValue(t), slot, initNode != nullptr, ctorArgs.value, region);
	}

	QueryResult<void> DeclLowerer::LowerValueLocalLifetime(const VariableDefinitionNode* n, const Type* t, uint32 slot,
		bool hasInitializer, const std::vector<ConstructionArgument>& ctorArgs, BlockNode* region)
	{
		// The zeroed local is constructed (unless it was initialized from a value), and destroyed when its
		// scope ends - on every exit edge, exceptions included (docs/language/06).
		Lifecycle life(m_Ctx);
		const LValue* self = SlotLValue(slot, t);
		const auto* obj = dynamic_cast<const ObjectType*>(t);
		if (!hasInitializer && !ctorArgs.empty() && obj)
		{
			if (auto r = ConstructFrom(obj, ctorArgs, false, self, region, n); !r.Ok()) return r;
		}
		else if (!hasInitializer)
		{
			if (auto allowed = AccessCheck::CheckDefaultCreate(m_Ctx, t); !allowed.Ok()) return m_Ctx.Fail(n, allowed);
			life.Construct(region, self);
		}
		m_Ctx.NoteCleanup(slot, life.ScheduleDestroy(region, self));
		return QueryResult<void>::Success();
	}

	QueryResult<void> DeclLowerer::LowerAggregateInit(const VariableDefinitionNode* n, const Type* t, const BracedInitializerNode* br, BlockNode* region)
	{
		const Core::String name = n->GetNameToken().GetText();
		const uint32 slot = m_Ctx.AllocSlot(t, name, SlotClass::Local);
		m_Ctx.out->SetStorageModifiers(slot, n->IsVolatile(), false);
		if (auto bound = BindName(n, name, t, slot, nullptr); !bound.Ok()) return bound;
		m_Ctx.jumps.NoteDeclaration("the declaration of '" + name + "'");
		auto decl = m_Ctx.out->NewNode<LocalDeclNode>(slot, t, nullptr);
		decl->m_Name = name;
		region->stmts.push_back(decl);

		const LValue* object = SlotLValue(slot, t);
		if (auto r = StartFromBraces(br, object, region, n, name); !r.Ok()) return r;
		m_Ctx.NoteCleanup(slot, Lifecycle(m_Ctx).ScheduleDestroy(region, object));
		return QueryResult<void>::Success();
	}

	// --- static storage ----------------------------------------------------------------------------------

	QueryResult<void> DeclLowerer::LowerStaticLocal(const VariableDefinitionNode* n, const Declarator& d, const Type* t, BlockNode* region)
	{
		const Core::String name = d.m_NameToken.GetText();
		const ReferenceType* ref = AsReference(t);
		const Type* slotType = ref ? m_Ctx.queries().TypeStore()->Pointer(TypeHandle::UnwrapToValue(ref->GetReferentType())) : t;
		const uint32 slot = m_Ctx.AllocSlot(slotType, name, SlotClass::Static);
		m_Ctx.out->SetStorageModifiers(slot, n->IsVolatile(), n->IsThreadLocal());
		BlockNode* once = m_Ctx.out->NewNode<BlockNode>(nullptr);
		auto started = ref ? StartStaticReference(n, d, ref, SlotLValue(slot, slotType), once)
			: StartStatic(StartOf(n, d), SlotLValue(slot, TypeHandle::UnwrapToValue(t)), once);
		if (!started.Ok()) return m_Ctx.Fail(n, started);
		if (auto bound = BindName(n, name, slotType, slot, ref); !bound.Ok()) return bound;
		auto decl = m_Ctx.out->NewNode<LocalDeclNode>(slot, slotType, nullptr);
		decl->m_Name = name;
		region->stmts.push_back(decl);
		if (once->stmts.empty() || StartsAsConstant(once, slot)) return QueryResult<void>::Success();
		region->stmts.push_back(m_Ctx.out->NewNode<OnceNode>(once, name, n->IsThreadLocal()));
		return QueryResult<void>::Success();
	}

	QueryResult<void> DeclLowerer::StartStatic(const VariableStart& start, const LValue* storage, BlockNode* body)
	{
		// A start that throws part-way leaves the storage zero again, so the next attempt starts from scratch.
		if (StorageCanBePartlyWritten(storage->type))
		{
			auto* reset = m_Ctx.out->NewNode<DeferNode>();
			reset->exceptionalOnly = true;
			reset->body = m_Ctx.out->NewNode<BlockNode>(nullptr);
			reset->body->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(storage, m_Ctx.out->NewNode<ConstantNode>(storage->type, ConstantValue())));
			body->stmts.push_back(reset);
		}
		return StartGlobal(start, storage, body);
	}

	QueryResult<void> DeclLowerer::StartGlobal(const VariableStart& start, const LValue* global, BlockNode* region)
	{
		Lifecycle life(m_Ctx);
		auto started = life.LowerStatement(region, [&] { return StartObject(start, global, region); });
		if (!started.Ok()) return started;
		life.DestroyAtExit(region, global);
		return started;
	}

	QueryResult<void> DeclLowerer::StartStaticReference(const VariableDefinitionNode* n, const Declarator& d,
		const ReferenceType* ref, const LValue* storage, BlockNode* body)
	{
		// `static T& r = x;`: the slot keeps the address, taken once.
		if (!d.m_Initializer) return QueryResult<void>::Success();
		return Lifecycle(m_Ctx).LowerStatement(body, [&]() -> QueryResult<void> {
			auto init = m_Ctx.Fail(n, ExprLowerer(m_Ctx).ResolveExpr(d.m_Initializer.Get()));
			if (!init.Ok()) return QueryResult<void>::Fail(init.error, init.message);
			auto address = ReferenceAddress(ref, &init.value, storage->type, d.m_NameToken.GetText());
			if (!address.Ok()) return QueryResult<void>::Fail(address.error, address.message);
			body->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(storage, address.value));
			return QueryResult<void>::Success();
		});
	}

	QueryResult<void> DeclLowerer::StartObject(const VariableStart& start, const LValue* target, BlockNode* body)
	{
		if (const auto* br = dynamic_cast<const BracedInitializerNode*>(start.initializer))
			return StartFromBraces(br, target, body, start.site, start.name);
		if (start.initializer) return StoreInitialValue(start, target, body);
		if (start.arguments)
		{
			auto argument = m_Ctx.Fail(start.site, ValueArgument(*start.arguments, target->type, start.name));
			if (!argument.Ok()) return QueryResult<void>::Fail(argument.error, argument.message);
			if (argument.value)
			{
				VariableStart fromValue = start;
				fromValue.initializer = argument.value;
				return StoreInitialValue(fromValue, target, body);
			}
		}
		const auto* obj = dynamic_cast<const ObjectType*>(target->type);
		if (obj && start.arguments && !start.arguments->Empty())
		{
			auto args = m_Ctx.Fail(start.site, ExprLowerer(m_Ctx).ConstructionArguments(*start.arguments));
			if (!args.Ok()) return QueryResult<void>::Fail(args.error, args.message);
			return ConstructFrom(obj, args.value, false, target, body, start.site);
		}
		if (auto allowed = AccessCheck::CheckDefaultCreate(m_Ctx, target->type); !allowed.Ok()) return m_Ctx.Fail(start.site, allowed);
		Lifecycle(m_Ctx).Construct(body, target);
		return QueryResult<void>::Success();
	}

	QueryResult<void> DeclLowerer::StoreInitialValue(const VariableStart& start, const LValue* target, BlockNode* body)
	{
		auto value = m_Ctx.Fail(start.site, ExprLowerer(m_Ctx).ResolveFor(start.initializer, target->type));
		if (!value.Ok()) return QueryResult<void>::Fail(value.error, value.message);
		auto initial = m_Ctx.Fail(start.site, InitialValue(m_Ctx, value.value, target->type));
		if (!initial.Ok()) return QueryResult<void>::Fail(initial.error, initial.message);
		ImplicitConversion::WarnIfNarrowing(m_Ctx, start.initializer, value.value, target->type);
		body->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(target, initial.value));
		return QueryResult<void>::Success();
	}

	bool DeclLowerer::StartsAsConstant(const BlockNode* body, uint32 slot)
	{
		// `static int32 calls = 0;` needs no guard: the storage starts holding the constant.
		if (body->stmts.size() != 1 || body->stmts[0]->kind != HIRNode::Kind::Store) return false;
		const auto* store = static_cast<const StoreNode*>(body->stmts[0]);
		const ConstantNode* constant = StoredConstant(store->value, m_Ctx.out->GetSlot(slot).type);
		if (!constant) return false;
		m_Ctx.out->SetStaticConstant(slot, constant);
		return true;
	}

	// The constant `value` is in `type`: a constant of it, or one converted to it (`null` or a string as a pointer,
	// a number as another number). Null when the value is computed.
	const ConstantNode* DeclLowerer::StoredConstant(const HIRNode* value, const Type* type)
	{
		if (const auto* constant = dynamic_cast<const ConstantNode*>(value)) return constant->type == type ? constant : nullptr;
		const auto* convert = dynamic_cast<const ConvertNode*>(value);
		const auto* source = convert ? dynamic_cast<const ConstantNode*>(convert->src) : nullptr;
		if (!source || convert->to != type) return nullptr;
		const TypeKind kind = TypeHandle::UnwrapToValue(type)->GetKind();
		if (kind == TypeKind::Pointer || kind == TypeKind::FunPtr)
		{
			const bool address = source->value.kind == ConstantValue::Kind::String ||
				(source->value.kind == ConstantValue::Kind::Int64 && source->value.intValue == 0);
			return address ? m_Ctx.out->NewNode<ConstantNode>(type, source->value) : nullptr;
		}
		auto converted = ConvertConstant(source->value, type);
		return converted.Ok() ? m_Ctx.out->NewNode<ConstantNode>(type, converted.value) : nullptr;
	}

	// --- construction ------------------------------------------------------------------------------------

	QueryResult<void> DeclLowerer::ConstructFrom(const ObjectType* obj, const std::vector<ConstructionArgument>& args, bool braced,
		const LValue* self, BlockNode* region, const AstNode* site)
	{
		ExprLowerer expr(m_Ctx);
		auto plan = PlanConstruction(m_Ctx, expr, obj, args, braced, site);
		if (!plan.Ok()) return QueryResult<void>::Fail(plan.error, plan.message);
		return m_Ctx.Fail(site, RunPlan(plan.value, self, region));
	}

	QueryResult<void> DeclLowerer::RunPlan(const ConstructionPlan& plan, const LValue* self, BlockNode* region)
	{
		const auto* obj = static_cast<const ObjectType*>(self->type);
		for (const HIRNode* step : plan.prelude) region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(step));
		if (plan.whole)
		{
			region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(m_Ctx.out->NewNode<StoreNode>(self, plan.whole)));
			return QueryResult<void>::Success();
		}
		if (plan.create)
		{
			auto* call = m_Ctx.out->NewNode<CallNode>(plan.create->type ? plan.create->type->GetReturnType() : nullptr, plan.create);
			if (auto bound = BindReceiver(m_Ctx, call, self, CallDispatch::Direct); !bound.Ok()) return bound;
			for (const HIRNode* a : plan.args) call->args.push_back(a);
			region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(call));
			return QueryResult<void>::Success();
		}
		size_t next = 0;
		for (const auto& field : obj->GetFields())
		{
			if (field.m_IsStatic || !field.m_Type || next >= plan.args.size()) continue;
			LValue fl; fl.kind = LValue::Kind::Field; fl.type = field.m_Type; fl.base = self; fl.field = &field;
			region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(m_Ctx.out->NewNode<StoreNode>(m_Ctx.NewLValue(fl), plan.args[next++])));
		}
		return QueryResult<void>::Success();
	}

	// --- names and references ----------------------------------------------------------------------------

	QueryResult<const HIRNode*> DeclLowerer::ReferenceAddress(const ReferenceType* ref, const Value* init,
		const Type* slotType, const Core::String& name)
	{
		using Result = QueryResult<const HIRNode*>;
		if (!init) return Result(static_cast<const HIRNode*>(nullptr));
		if (!ref->IsConstReferent() && init->isLValue && IsReadOnly(m_Ctx, init->lvalue))
			return Result::Fail(QueryError::NotAValue,
				"cannot bind a non-const reference to a const object: '" + name + "' would allow mutating read-only data");
		// A reference binds to a LOCATION (a call returning `T&` is one).
		if (!init->isLValue)
			return Result::Fail(QueryError::NotAValue,
				"cannot bind a reference to a prvalue: '" + name + "' must initialize from an lvalue (a location), not a temporary");
		if (Core::String why = BindingProblem(ref, TypeHandle::UnwrapToValue(init->type)); !why.empty()) return Result::Fail(QueryError::NotAValue, why);
		return Result(m_Ctx.AddressAs(slotType, init->lvalue));
	}

	// A reference local names a location of its referent's type, or a derived object through its base (04 4.4); a
	// conversion would make a temporary, which a reference local cannot hold.
	Core::String DeclLowerer::BindingProblem(const ReferenceType* ref, const Type* located)
	{
		const Type* referent = TypeHandle::UnwrapToValue(ref->GetReferentType());
		if (!located || located == referent || referent->GetKind() == TypeKind::Void) return "";
		const auto* object = dynamic_cast<const ObjectType*>(located);
		const auto* base = dynamic_cast<const ObjectType*>(referent);
		if (object && base && ClassHierarchy::DerivesFrom(object, base)) return "";
		return "cannot bind '" + ref->GetName() + "' to a '" + located->GetName() + "': a reference names a location of its own type";
	}

	QueryResult<void> DeclLowerer::BindName(const VariableDefinitionNode* n, const Core::String& name, const Type* type,
		uint32 slot, const ReferenceType* ref)
	{
		return BindSlot(n, name, type, slot, ref, n->IsConst());
	}

	QueryResult<void> DeclLowerer::BindSlot(const AstNode* site, const Core::String& name, const Type* type, uint32 slot, const ReferenceType* ref,
		bool isConst)
	{
		SymbolInfo sym; sym.kind = SymbolKind::Variable; sym.type = type; sym.isConst = isConst; sym.definingUnit = 0;
		if (m_Ctx.BindLocal(name, sym, slot, ref != nullptr, ref && ref->IsConstReferent())) return QueryResult<void>::Success();
		return m_Ctx.Fail(site, QueryResult<void>::Fail(QueryError::NotAValue, "Variable '" + name + "' is already declared in this block"));
	}

	QueryResult<void> DeclLowerer::DeclareValue(const LocalStart& local, const Value& value, BlockNode* region)
	{
		// As `T name = value;`: the slot starts from the value (a fresh object relocates), and the variable is
		// destroyed when its scope ends.
		auto initial = m_Ctx.Fail(local.site, InitialValue(m_Ctx, value, local.type));
		if (!initial.Ok()) return QueryResult<void>::Fail(initial.error, initial.message);
		const uint32 slot = m_Ctx.AllocSlot(local.type, local.name, SlotClass::Local);
		if (auto bound = BindSlot(local.site, local.name, local.type, slot, nullptr, local.isConst); !bound.Ok()) return bound;
		m_Ctx.jumps.NoteDeclaration("the declaration of '" + local.name + "'");
		auto decl = m_Ctx.out->NewNode<LocalDeclNode>(slot, local.type, initial.value);
		decl->m_Name = local.name;
		region->stmts.push_back(decl);
		m_Ctx.NoteCleanup(slot, Lifecycle(m_Ctx).ScheduleDestroy(region, SlotLValue(slot, TypeHandle::UnwrapToValue(local.type))));
		return QueryResult<void>::Success();
	}

	const LValue* DeclLowerer::SlotLValue(uint32 slot, const Type* type)
	{
		LValue lv; lv.kind = LValue::Kind::Slot; lv.type = type; lv.slot = slot;
		return m_Ctx.NewLValue(lv);
	}
} // namespace Scripting
