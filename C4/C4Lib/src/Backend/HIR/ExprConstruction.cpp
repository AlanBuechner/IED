#include "Backend/HIR/ExprLowerer.h"
#include <tuple>
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/NewArray.h"
#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/LambdaLowerer.h"
#include "Backend/HIR/FoldLowerer.h"
#include "Backend/HIR/PackElements.h"
#include "Backend/HIR/VarArgs.h"
#include "Backend/HIR/BuiltinOperators.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Model/Abstractness.h"
#include "Parser/Nodes/Expressions/NewExpressionNode.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h" // init-list call arguments
#include "Parser/Nodes/Expressions/ValueListNode.h"
#include "Model/Types/ReturnLists.h"
#include "Model/Types/QualifiedName.h"
#include "Model/TypeHandle.h"

namespace Scripting
{
	QueryResult<Value> ExprLowerer::ResolveValueList(const ValueListNode* n, const ObjectType* list)
	{
		// Each value starts its element, converted as a braced entry is (`T{e0, e1}`); a list gives all of them.
		auto args = ConstructionArguments(n->GetElements());
		if (!args.Ok()) return QueryResult<Value>::Fail(args.error, args.message);
		if (args.value.size() != list->GetFields().Count())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "the return list '" + list->GetName() + "' has " +
				std::to_string(list->GetFields().Count()) + " values, got " + std::to_string(args.value.size())));
		return ResolveConstruct(n, list, args.value, true);
	}

	QueryResult<Value> ExprLowerer::ResolveValueList(const ValueListNode* n)
	{
		// A list nothing gives a type to (`[b, a]` destructured) is the return list of its values' types.
		auto args = ConstructionArguments(n->GetElements());
		if (!args.Ok()) return QueryResult<Value>::Fail(args.error, args.message);
		std::vector<const Type*> types;
		for (const ConstructionArgument& arg : args.value)
		{
			if (arg.brace)
				return m_Ctx.Fail(arg.brace, QueryResult<Value>::Fail(QueryError::NotAValue, "a braced list in '" + n->ToString() +
					"' has no type of its own: write the value it makes"));
			types.push_back(TypeHandle::UnwrapToValue(arg.value.type));
		}
		auto list = ReturnLists::Of(m_Ctx.model(), types);
		if (!list.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(list.error, list.message));
		return ResolveConstruct(n, list.value, args.value, true);
	}

	// `new T(...)` (05): allocate; the result is a POINTER rvalue (`*p` / `p->x` lower via Deref). `new T[n]`: NewArray.
	QueryResult<Value> ExprLowerer::ResolveNew(const NewExpressionNode* n)
	{
		// The object is constructed in place (docs/language/06). A heap object's full destruction is registered
		// as the GC finalizer, so `delete p` and reclamation both run it exactly once; storage given to
		// `new (p) T` belongs to the caller, who destroys the object with destroy(p).
		auto t = m_Ctx.Fail(n, m_Ctx.queries().ResolveTypeCtx(*n->GetAllocType().Get(), m_Ctx.unitId, m_Ctx.scope));
		if (!t.Ok()) return QueryResult<Value>::Fail(t.error, t.message);
		const Type* objT = TypeHandle::UnwrapToValue(t.value);
		if (Core::String why = Abstractness::Creation(m_Ctx.model(), objT); !why.empty())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, why));
		const Type* ptrT = m_Ctx.queries().TypeStore()->Pointer(objT);
		const HIRNode* placement = nullptr;
		if (n->GetPlacement())
		{
			auto storage = PlacementStorage(n, ptrT);
			if (!storage.Ok()) return QueryResult<Value>::Fail(storage.error, storage.message);
			placement = storage.value;
		}
		if (n->IsArray()) return NewArrayLowerer(m_Ctx, *this).Lower(n, objT, placement);
		auto* nw = m_Ctx.out->NewNode<NewNode>(ptrT, objT);
		nw->placement = placement;
		auto args = ConstructionArguments(n->GetArguments());
		if (!args.Ok()) return QueryResult<Value>::Fail(args.error, args.message);
		const auto* obj = dynamic_cast<const ObjectType*>(objT);
		if (!obj) return NewScalar(n, nw, args.value);
		return NewObject(n, nw, obj, args.value, n->HasBraces());
	}

	QueryResult<Value> ExprLowerer::NewObject(const AstNode* site, NewNode* nw, const ObjectType* obj, const std::vector<ConstructionArgument>& args,
		bool braced)
	{
		auto plan = PlanConstruction(m_Ctx, *this, obj, args, braced, site);
		if (!plan.Ok()) return QueryResult<Value>::Fail(plan.error, plan.message);
		if (!nw->placement) nw->finalizer = Lifecycle(m_Ctx).Destroy(obj);
		if (plan.value.whole) return NewHolding(nw, plan.value.whole);
		nw->ctor = plan.value.create;
		nw->aggregateInit = plan.value.aggregate;
		nw->args = plan.value.args;
		return Value::R(AfterPrelude(plan.value.prelude, nw, false), nw->type);
	}

	QueryResult<Value> ExprLowerer::PlacementNew(const AstNode* site, const ObjectType* type, const HIRNode* storage,
		const std::vector<ConstructionArgument>& args)
	{
		auto* nw = m_Ctx.out->NewNode<NewNode>(m_Ctx.queries().TypeStore()->Pointer(type), type);
		nw->placement = storage;
		return NewObject(site, nw, type, args, false);
	}

	const HIRNode* ExprLowerer::AfterPrelude(const std::vector<const HIRNode*>& prelude, const HIRNode* value, bool fresh)
	{
		if (prelude.empty()) return value;
		auto* seq = m_Ctx.out->NewNode<SequenceNode>(value->type);
		seq->steps = prelude;
		seq->steps.push_back(value);
		seq->fresh = fresh;
		return seq;
	}

	QueryResult<const HIRNode*> ExprLowerer::PlacementStorage(const NewExpressionNode* n, const Type* ptrT)
	{
		auto storage = ResolveExpr(n->GetPlacement().Get());
		if (!storage.Ok()) return QueryResult<const HIRNode*>::Fail(storage.error, storage.message);
		const HIRNode* address = m_Ctx.LoadIfNeeded(storage.value);
		const Type* given = TypeHandle::UnwrapToValue(address->type);
		if (!given || given->GetKind() != TypeKind::Pointer)
			return m_Ctx.Fail(n, QueryResult<const HIRNode*>::Fail(QueryError::NotAValue,
				"'new (storage) T' needs a pointer to the storage, got '" + (given ? given->GetName() : Core::String("?")) + "'"));
		return QueryResult<const HIRNode*>(m_Ctx.out->NewNode<ConvertNode>(address, address->type, ptrT));
	}

	QueryResult<Value> ExprLowerer::NewScalar(const AstNode* n, NewNode* nw, const std::vector<ConstructionArgument>& args)
	{
		// `new int32(5)`: the allocation, then its one initial value through the new pointer.
		if (args.empty()) return Value::R(nw, nw->type);
		if (args.size() > 1 || args[0].brace)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "'new " + nw->objectType->GetName() + "' takes at most one initial value"));
		auto value = InitialValue(m_Ctx, args[0].value, nw->objectType);
		if (!value.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(value.error, value.message));
		return NewHolding(nw, value.value);
	}

	Value ExprLowerer::NewHolding(NewNode* nw, const HIRNode* value)
	{
		// The new storage (zeroed, vptrs set), then `value` stored into it; the result is the pointer.
		uint32 slot = m_Ctx.AllocSlot(nw->type, "__new", SlotClass::Temp);
		LValue ptrLv; ptrLv.kind = LValue::Kind::Slot; ptrLv.type = nw->type; ptrLv.slot = slot;
		const LValue* ptr = m_Ctx.NewLValue(ptrLv);
		LValue target; target.kind = LValue::Kind::Deref; target.type = nw->objectType; target.addr = m_Ctx.out->NewNode<LoadNode>(nw->type, ptr);
		auto* seq = m_Ctx.out->NewNode<SequenceNode>(nw->type);
		seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(ptr, nw));
		seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(m_Ctx.NewLValue(target), value));
		seq->steps.push_back(m_Ctx.out->NewNode<LoadNode>(nw->type, ptr));
		return Value::R(seq, nw->type);
	}

	QueryResult<std::vector<ConstructionArgument>> ExprLowerer::ConstructionArguments(const Core::Vector<Ref<AstNode>>& nodes)
	{
		std::vector<ConstructionArgument> args;
		for (const auto& node : nodes)
		{
			ConstructionArgument arg;
			if (const auto* brace = dynamic_cast<const BracedInitializerNode*>(node.Get()))
			{
				arg.brace = brace;
				args.push_back(arg);
				continue;
			}
			auto values = ResolveArgument(node.Get());
			if (!values.Ok()) return QueryResult<std::vector<ConstructionArgument>>::Fail(values.error, values.message);
			for (const Value& value : values.value)
			{
				arg.value = ThisAsPointer(value);   // `this` is the receiver's address, as in a call (BUG-412)
				args.push_back(arg);
			}
		}
		return args;
	}

	// `T(args)` / `T{...}` as an EXPRESSION: the object is constructed in a temporary and yielded by value
	// (a fresh object the consumer relocates or destroys).
	QueryResult<Value> ExprLowerer::ResolveConstruct(const AstNode* callee, const ObjectType* obj, const std::vector<ConstructionArgument>& args, bool braced)
	{
		auto plan = PlanConstruction(m_Ctx, *this, obj, args, braced, callee);
		if (!plan.Ok()) return QueryResult<Value>::Fail(plan.error, plan.message);
		if (plan.value.whole) return Value::R(plan.value.whole, obj);
		auto* cn = m_Ctx.out->NewNode<ConstructNode>(obj);
		cn->ctor = plan.value.create;
		cn->aggregateInit = plan.value.aggregate;
		cn->args = plan.value.args;
		return Value::R(AfterPrelude(plan.value.prelude, cn, true), obj);
	}

	QueryResult<Value> ExprLowerer::ResolveBracedAs(const BracedInitializerNode* brace, const Type* target)
	{
		const Type* t = TypeHandle::UnwrapToValue(target);
		if (t && t->GetKind() == TypeKind::InitList) return ResolveInitList(brace, t);
		if (const auto* obj = dynamic_cast<const ObjectType*>(t)) return ResolveBracedObject(brace, obj, brace);
		if (t && t->GetKind() == TypeKind::Array)
		{
			auto value = DeclLowerer(m_Ctx).BracedValue(brace, t, brace);
			if (!value.Ok()) return QueryResult<Value>::Fail(value.error, value.message);
			return Value::R(value.value, t);
		}
		return m_Ctx.Fail(brace, QueryResult<Value>::Fail(QueryError::NotAValue,
			"a braced list cannot initialize '" + (t ? t->GetName() : Core::String("?")) + "' here"));
	}

	QueryResult<Value> ExprLowerer::ResolveBracedObject(const AstNode* site, const ObjectType* obj, const BracedInitializerNode* brace)
	{
		// Positional elements construct directly; designated ones fill the fields as a declaration does.
		if (brace->HasDesignators())
		{
			auto value = DeclLowerer(m_Ctx).BracedValue(brace, obj, site);
			if (!value.Ok()) return QueryResult<Value>::Fail(value.error, value.message);
			return Value::R(value.value, obj);
		}
		auto args = ConstructionArguments(brace->GetElements());
		if (!args.Ok()) return QueryResult<Value>::Fail(args.error, args.message);
		return ResolveConstruct(site, obj, args.value, true);
	}

	QueryResult<Value> ExprLowerer::ResolveInitList(const BracedInitializerNode* n, const Type* target)
	{
		if (!target)
			return QueryResult<Value>::Fail(QueryError::NotAValue, "braced initializer list has no __init_list<T> parameter to bind to");
		const Type* ilType = target;
		if (ilType->GetKind() == TypeKind::Pointer)
		{
			// const-reference FFI form: C++ `const std::initializer_list<T>&` maps to __init_list<T>*.
			ilType = static_cast<const PointerType*>(target)->GetPointeeType();
			if (!ilType || ilType->GetKind() != TypeKind::InitList)
				return QueryResult<Value>::Fail(QueryError::NotAValue, "braced initializer list has no __init_list<T> parameter to bind to");
		}
		else if (ilType->GetKind() != TypeKind::InitList)
			return QueryResult<Value>::Fail(QueryError::NotAValue, "braced initializer list has no __init_list<T> parameter to bind to");
		const Type* elemType = static_cast<const InitListType*>(ilType)->GetElementType();

		auto node = m_Ctx.out->NewNode<InitListNode>(ilType);
		for (const auto& e : n->GetEntries())
		{
			if (e.m_Key)
				return QueryResult<Value>::Fail(QueryError::NotAValue, "keyed initializer is not valid in an init list");
			auto added = AppendInitListElements(e.m_Value.Get(), elemType, node->elements);
			if (!added.Ok()) return QueryResult<Value>::Fail(added.error, added.message);
		}
		return Value::R(node, ilType); // ilType == target unless the param was __init_list<T>* (const-ref FFI form)
	}

	// One entry of a braced list that builds an __init_list<T>: a braced entry builds a T (C++'s nested braces), a
	// pack expansion gives one element per value, and any other entry initializes a T as a variable would.
	QueryResult<void> ExprLowerer::AppendInitListElements(const AstNode* entry, const Type* elemType, std::vector<const HIRNode*>& elements)
	{
		if (const auto* inner = dynamic_cast<const BracedInitializerNode*>(entry))
		{
			auto element = ResolveBracedAs(inner, elemType);
			if (!element.Ok()) return QueryResult<void>::Fail(element.error, element.message);
			elements.push_back(m_Ctx.LoadIfNeeded(element.value));
			return QueryResult<void>::Success();
		}
		auto values = ResolveArgument(entry);
		if (!values.Ok()) return QueryResult<void>::Fail(values.error, values.message);
		for (const Value& value : values.value)
		{
			auto element = m_Ctx.Fail(entry, InitialValue(m_Ctx, value, elemType));
			if (!element.Ok()) return QueryResult<void>::Fail(element.error, element.message);
			elements.push_back(element.value);
		}
		return QueryResult<void>::Success();
	}
} // namespace Scripting
