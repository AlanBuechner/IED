#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/Lifecycle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/TypeHandle.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"

namespace Scripting
{
	// A qualified call target `Ns::Fn` (or nested `A::B::C`) parses as a StaticAccessNode whose base is
	// either another StaticAccessNode or a plain identifier. Join the path into "A::B::C" for QueryFunction.
	bool ExprLowerer::QualifiedCallName(const StaticAccessNode* sa, Core::String& out)
	{
		const AstNode* base = sa->GetBaseType().Get();
		if (!base) return false;
		const auto* inner = NodeAs<StaticAccessNode>(base);
		if (inner)
		{
			if (!QualifiedCallName(inner, out)) return false; // out now holds "A::B"
		}
		else if (const auto* id = NodeAs<IdentifierExpressionNode>(base))
			out = id->GetName();                              // out now holds "M"
		else
			return false; // a qualified TYPE base (e.g. `Ns::Vec2`) is not a function call target here
		out += "::";                                          // always separate qualifier from the member
		out += sa->GetMemberNameToken().GetText();
		return true;
	}

	bool ExprLowerer::IsCallableValue(const Type* t)
	{
		t = TypeHandle::UnwrapToValue(t);
		return dynamic_cast<const SignatureType*>(t) || TypeAs<ObjectType>(t);
	}

	bool ExprLowerer::IsCallableField(const Type* owner, const Core::String& name) const
	{
		const auto* obj = TypeAs<ObjectType>(owner);
		if (!obj) return false;
		ClassHierarchy::FieldHit hit = ClassHierarchy::FindField(obj, name);
		return hit.field && IsCallableValue(hit.field->m_Type);
	}

	QueryResult<ExprLowerer::CalleeValue> ExprLowerer::CalleeFromValue(const Value& v)
	{
		using Result = QueryResult<CalleeValue>;
		CalleeValue c;
		const Type* t = TypeHandle::UnwrapToValue(v.type);
		if (const auto* signature = dynamic_cast<const SignatureType*>(t))
		{
			c.target = m_Ctx.LoadIfNeeded(v);
			c.signature = signature;
			return Result::Success(c);
		}
		const auto* obj = TypeAs<ObjectType>(t);
		if (!obj)
			return Result::Fail(QueryError::NotCallable, "a '" + Core::String(t ? t->GetName() : "?") +
				"' cannot be called: only functions, funcptrs, funcs and objects with a call operator can");
		bool callable = false;
		for (const auto& m : obj->GetMethods()) callable = callable || m.HasOperator("call");
		if (!callable)
			return Result::Fail(QueryError::NotCallable, "'" + obj->GetName() + "' cannot be called: it has no [operator(\"call\")] method");
		c.object = obj;
		c.objectLocation = v.isLValue ? v.lvalue : Lifecycle(m_Ctx).AddressableObject(v, obj);
		return Result::Success(c);
	}

	QueryResult<ExprLowerer::CallTarget> ExprLowerer::ResolveCallTarget(const AstNode* callee)
	{
		if (const auto* id = NodeAs<IdentifierExpressionNode>(callee)) return NamedCallTarget(id);
		if (const auto* ma = NodeAs<MemberAccessNode>(callee)) return MemberCallTarget(ma);
		if (const auto* sa = NodeAs<StaticAccessNode>(callee)) return StaticCallTarget(sa);
		if (const auto* qn = NodeAs<NamespaceQualifiedNameNode>(callee))
		{
			// `Ns::F<args>(...)`: the parser keeps a namespaced template call's target as one qualified name.
			CallTarget target;
			target.qualified = true;
			for (const auto& seg : qn->GetSegments()) target.name += (target.name.empty() ? "" : "::") + seg.nameToken.GetText();
			return target;
		}
		// Any other callee is a value: a call result (`MakeAdder(1)(2)`), an element (`table[0](x)`), a lambda.
		return ValueCallTarget(callee);
	}

	// A funcptr or func is called through; an object becomes the receiver of its `operator_call`.
	QueryResult<void> ExprLowerer::CallThrough(const AstNode* callee, const Value& value, CallTarget& target)
	{
		auto called = CalleeFromValue(value);
		if (!called.Ok()) return m_Ctx.Fail(callee, QueryResult<void>::Fail(called.error, called.message));
		target.indirect = called.value.target;
		target.signature = called.value.signature;
		if (!called.value.object) return QueryResult<void>::Success();
		target.name = "operator_call";
		target.receiverType = called.value.object;
		target.receiverLoc = called.value.objectLocation;
		return QueryResult<void>::Success();
	}

	QueryResult<ExprLowerer::CallTarget> ExprLowerer::ValueCallTarget(const AstNode* callee)
	{
		auto value = ResolveExpr(callee);
		if (!value.Ok()) return QueryResult<CallTarget>::Fail(value.error, value.message);
		CallTarget target;
		if (auto through = CallThrough(callee, value.value, target); !through.Ok()) return QueryResult<CallTarget>::Fail(through.error, through.message);
		return target;
	}

	bool ExprLowerer::NamesVariable(const Core::String& name)
	{
		auto variable = m_Ctx.queries().QueryVariable(name, nullptr, m_Ctx.unitId, m_Ctx.enclosingType, m_Ctx.scope);
		return variable.Ok() && variable.value.type;
	}

	// `op(a, b)` where `op` is a variable calls its value; otherwise the name is a function's.
	QueryResult<ExprLowerer::CallTarget> ExprLowerer::NamedCallTarget(const IdentifierExpressionNode* id)
	{
		if (NamesVariable(id->GetName())) return ValueCallTarget(id);
		CallTarget target;
		target.name = id->GetName();
		return target;
	}

	QueryResult<void> ExprLowerer::CheckTypeReceiver(const MemberAccessNode* ma)
	{
		const auto* id = dynamic_cast<const IdentifierExpressionNode*>(ma->GetBaseObject().Get());
		if (!id || NamesVariable(id->GetName())) return QueryResult<void>::Success();
		auto type = m_Ctx.queries().QueryType(id->GetName(), m_Ctx.unitId, m_Ctx.scope);
		if (!type.Ok() || !dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(type.value))) return QueryResult<void>::Success();
		const Core::String call = id->GetName() + "::" + ma->GetMemberNameToken().GetText();
		return QueryResult<void>::Fail(QueryError::NotCallable, "'" + id->GetName() + "' is a type, not an object: call its static method as " + call + "(...)");
	}

	QueryResult<ExprLowerer::CallTarget> ExprLowerer::MemberCallTarget(const MemberAccessNode* ma)
	{
		if (auto onType = CheckTypeReceiver(ma); !onType.Ok()) return m_Ctx.Fail(ma, QueryResult<CallTarget>::Fail(onType.error, onType.message));
		auto base = ResolveExpr(ma->GetBaseObject().Get());
		if (!base.Ok()) return QueryResult<CallTarget>::Fail(base.error, base.message);
		if (auto spelled = CheckAccessOperator(ma, base.value); !spelled.Ok())
			return m_Ctx.Fail(ma, QueryResult<CallTarget>::Fail(spelled.error, spelled.message));
		CallTarget target;
		DerefForAccess(base.value, target.receiverType, target.receiverLoc); // a pointer base: `p->m()` finds the method
		target.name = ma->GetMemberNameToken().GetText();
		// A field holding a callable (`h.callback(v)`) is called as a value.
		if (IsCallableField(target.receiverType, target.name))
		{
			target = CallTarget();
			auto value = ResolveExpr(ma);
			if (!value.Ok()) return QueryResult<CallTarget>::Fail(value.error, value.message);
			if (auto through = CallThrough(ma, value.value, target); !through.Ok()) return QueryResult<CallTarget>::Fail(through.error, through.message);
		}
		// A method of a by-value call result (`Make(5).Equals(a)`) runs on a temporary holding it.
		if (!target.receiverLoc && base.value.IsRValue() && target.receiverType && target.name != "operator_call")
			target.receiverLoc = Lifecycle(m_Ctx).AddressableObject(base.value, target.receiverType);
		// `p->Base::F()`: the named type's implementation, called on that sub-object, never dispatched (B35).
		if (ma->IsQualified() && target.receiverType)
		{
			auto viewed = QualifiedSubobject(ma, target.receiverType, target.receiverLoc);
			if (!viewed.Ok()) return m_Ctx.Fail(ma, QueryResult<CallTarget>::Fail(viewed.error, viewed.message));
			target.receiverType = viewed.value.type;
			target.receiverLoc = viewed.value.location;
			target.qualified = true;
		}
		return target;
	}

	// `Ns::Fn(...)`, `T::Static(...)`, `Box<T>::Make(...)`: a qualified call names one implementation.
	QueryResult<ExprLowerer::CallTarget> ExprLowerer::StaticCallTarget(const StaticAccessNode* sa)
	{
		CallTarget target;
		target.qualified = true;
		if (const Type* bound = BoundTypeNamed(sa->GetBaseType().Get()))
		{
			target.receiverType = TypeHandle::UnwrapToValue(bound);
			target.name = sa->GetMemberNameToken().GetText();
			return target;
		}
		if (const ObjectType* instance = AliasedInstance(sa->GetBaseType().Get()))
		{
			if (IsCallableField(instance, sa->GetMemberNameToken().GetText())) return ValueCallTarget(sa);
			target.receiverType = instance;
			target.name = sa->GetMemberNameToken().GetText();
			return target;
		}
		// `Ns::handler(x)` with `handler` a variable calls its value, as the plain name does (BUG-316).
		if (QualifiedCallName(sa, target.name)) return NamesVariable(target.name) ? ValueCallTarget(sa) : target;
		// The base is a type (`TBox<int32>::MakeVal(...)`, `Outer<int32>::Inner::Make(...)`): the member is one of its
		// methods. A static method has no implicit this, so the type as receiver finds it and passes no object.
		target.name.clear();
		auto owner = QualifierType(sa->GetBaseType().Get());
		if (!owner.Ok()) return m_Ctx.Fail(sa, QueryResult<CallTarget>::Fail(owner.error, owner.message));
		if (!owner.value) return m_Ctx.Fail(sa, QueryResult<CallTarget>::Fail(QueryError::NotCallable, "call target is not a name or member"));
		target.receiverType = owner.value;
		target.name = sa->GetMemberNameToken().GetText();
		if (IsCallableField(owner.value, target.name)) return ValueCallTarget(sa); // `T::handler(x)`: a static field's value
		return target;
	}
} // namespace Scripting
