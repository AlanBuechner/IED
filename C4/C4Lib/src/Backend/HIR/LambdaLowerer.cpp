#include "Backend/HIR/LambdaLowerer.h"
#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/SimpleStatementNodes.h"
#include "Parser/Nodes/Statements/IfStatementNode.h"
#include "Parser/Nodes/Statements/WhileLoopNode.h"
#include "Parser/Nodes/Statements/ForLoopNode.h"
#include "Parser/Nodes/Statements/TryCatchNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		constexpr const char* kThis = "this";

		// A lambda without `-> Ret` is void (C4 does not deduce it), so a value return is an error.
		bool BodyHasValueReturn(const AstNode* s)
		{
			if (!s) return false;
			switch (s->Type())
			{
			case NodeType::ReturnStatement: return static_cast<const ReturnStatementNode*>(s)->HasValue();
			case NodeType::CodeBlock:
				for (const auto& st : static_cast<const CodeBlockNode*>(s)->GetStatements())
					if (BodyHasValueReturn(st.Get())) return true;
				return false;
			case NodeType::IfStatement:
			{
				const auto* ifn = static_cast<const IfStatementNode*>(s);
				return BodyHasValueReturn(ifn->GetThenSingleStatement().Get()) || BodyHasValueReturn(ifn->GetThenBody().Get()) ||
					BodyHasValueReturn(ifn->GetElseBranch().Get());
			}
			case NodeType::WhileLoop: return BodyHasValueReturn(static_cast<const WhileLoopNode*>(s)->GetBody().Get());
			case NodeType::ForLoop: return BodyHasValueReturn(static_cast<const ForLoopNode*>(s)->GetBody().Get());
			case NodeType::DoWhileLoop:
			{
				const auto* dw = static_cast<const DoWhileLoopNode*>(s);
				return BodyHasValueReturn(dw->GetBody().Get()) || BodyHasValueReturn(dw->GetSingleStatementBody().Get());
			}
			case NodeType::TryCatch:
			{
				const auto* tc = static_cast<const TryCatchNode*>(s);
				if (BodyHasValueReturn(tc->GetTryBody().Get()) || BodyHasValueReturn(tc->GetFinallyBody().Get())) return true;
				for (const auto& cl : tc->GetCatchClauses())
					if (BodyHasValueReturn(cl->GetBody().Get()) || BodyHasValueReturn(cl->GetSingleStatementBody().Get())) return true;
				return false;
			}
			default: return false;
			}
		}

		bool HasInstanceMember(const ObjectType* type, const Core::String& name)
		{
			if (!type) return false;
			ClassHierarchy::FieldHit field = ClassHierarchy::FindField(type, name);
			if (field.field) return !field.field->m_IsStatic;
			for (const auto& m : type->GetMethods())
				if (m.mName == name && !m.m_IsStatic) return true;
			for (const ObjectType* base : type->GetBaseTypes())
				if (HasInstanceMember(base, name)) return true;
			return false;
		}

		bool Captured(const std::vector<Core::String>& names, const Core::String& name)
		{
			return std::find(names.begin(), names.end(), name) != names.end();
		}
	}

	QueryResult<Value> LambdaLowerer::Lower(const LambdaNode& lambda)
	{
		if (!dynamic_cast<const CodeBlockNode*>(lambda.GetBody().Get()))
			return m_Ctx.Fail(&lambda, QueryResult<Value>::Fail(QueryError::NotAValue, "lambda has no body"));
		ClosureSpec spec;
		auto signature = Signature(lambda, spec.paramNames, spec.paramIsConst);
		if (!signature.Ok()) return m_Ctx.Fail(&lambda, QueryResult<Value>::Fail(signature.error, signature.message));
		auto plan = PlanCaptures(lambda);
		if (!plan.Ok()) return m_Ctx.Fail(&lambda, QueryResult<Value>::Fail(plan.error, plan.message));

		spec.signature = signature.value;
		spec.body = lambda.GetBody().Get();
		spec.scope = m_Ctx.scope;
		spec.unitId = m_Ctx.unitId;
		spec.isMutable = lambda.IsMutable();
		spec.owner = m_Ctx.CodeSymbol();
		spec.ownerName = OwnerName();
		std::vector<ConstructionArgument> fields;
		for (const PlannedCapture& p : plan.value)
		{
			spec.captures.Push(p.capture);
			fields.push_back(p.init);
			if (p.capture.kind == ClosureCapture::Kind::This || p.capture.kind == ClosureCapture::Kind::ThisCopy)
				spec.enclosingType = m_Ctx.enclosingType;
		}
		const Closure* closure = m_Ctx.model().CreateClosure(spec);
		if (auto body = LowerCallMethod(*closure); !body.Ok()) return QueryResult<Value>::Fail(body.error, body.message);
		return m_Expr.ResolveConstruct(&lambda, closure->type, fields, true);
	}

	// The code the lambda is written in: a function by its qualified name, a lambda's body by that lambda's name.
	Core::String LambdaLowerer::OwnerName() const
	{
		const FunctionRecord* fn = m_Ctx.fn;
		if (fn && fn->containingType)
			if (const Closure* outer = m_Ctx.model().ClosureOf(fn->containingType)) return outer->displayName;
		if (fn) return QualifiedName(*fn);
		return m_Ctx.startedGlobal ? "the start of " + m_Ctx.startedGlobal->plainName : Core::String("the program's start");
	}

	QueryResult<const FunctionType*> LambdaLowerer::Signature(const LambdaNode& lambda, Core::Vector<Core::String>& paramNames,
		std::vector<bool>& paramIsConst)
	{
		using Result = QueryResult<const FunctionType*>;
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* ret = ts->Builtin(TypeKind::Void);
		if (lambda.GetReturnType())
		{
			auto rt = m_Ctx.queries().ResolveTypeCtx(*static_cast<const QualTypeNode*>(lambda.GetReturnType().Get()), m_Ctx.unitId, m_Ctx.scope);
			if (!rt.Ok()) return Result::Fail(rt.error, rt.message);
			ret = TypeHandle::UnwrapToValue(rt.value);
		}
		else if (BodyHasValueReturn(lambda.GetBody().Get()))
			return Result::Fail(QueryError::NotAValue, "lambda returns a value but has no explicit return type: add '-> <type>' "
				"before the body (C4 does not deduce lambda return types)");

		Core::Vector<FunctionType::ParameterInfo> params;
		for (uint32 i = 0; i < lambda.ParameterCount(); ++i)
		{
			const auto& p = lambda.GetParameters()[i];
			const auto& type = *static_cast<const QualTypeNode*>(p->GetType().Get());
			auto pt = m_Ctx.queries().ResolveTypeCtx(type, m_Ctx.unitId, m_Ctx.scope);
			if (!pt.Ok()) return Result::Fail(pt.error, pt.message);
			paramIsConst.push_back(type.DeclaresConstValue());
			FunctionType::ParameterInfo info;
			info.mName = p->HasName() ? p->GetNameToken().GetText() : "arg" + std::to_string(i);
			info.m_Type = pt.value;
			params.Push(info);
			paramNames.Push(info.mName);
		}
		return Result::Success(static_cast<const FunctionType*>(ts->Own(new FunctionType(ret, params))));
	}

	// --- captures ------------------------------------------------------------------------------------------

	QueryResult<LambdaLowerer::Plan> LambdaLowerer::PlanCaptures(const LambdaNode& lambda)
	{
		Plan plan;
		if (auto listed = PlanListed(lambda, plan); !listed.Ok()) return QueryResult<Plan>::Fail(listed.error, listed.message);
		if (auto defaults = PlanDefaults(lambda, FindOuterUses(lambda), plan); !defaults.Ok())
			return QueryResult<Plan>::Fail(defaults.error, defaults.message);
		return QueryResult<Plan>::Success(plan);
	}

	QueryResult<void> LambdaLowerer::PlanListed(const LambdaNode& lambda, Plan& plan)
	{
		std::vector<Core::String> names;
		for (const LambdaCapture& c : lambda.GetCaptures())
		{
			const bool isThis = c.kind == LambdaCapture::Kind::This || c.kind == LambdaCapture::Kind::ThisCopy;
			const Core::String name = isThis ? Core::String(kThis) : c.name.GetText();
			if (Captured(names, name))
				return QueryResult<void>::Fail(QueryError::Ambiguous, "'" + name + "' is captured more than once");
			names.push_back(name);
			auto planned = isThis ? CaptureThis(c.kind == LambdaCapture::Kind::ThisCopy)
				: c.kind == LambdaCapture::Kind::Init ? CaptureInit(c)
				: CaptureVariable(name, c.byReference);
			if (!planned.Ok()) return QueryResult<void>::Fail(planned.error, planned.message);
			plan.push_back(planned.value);
		}
		return QueryResult<void>::Success();
	}

	QueryResult<void> LambdaLowerer::PlanDefaults(const LambdaNode& lambda, const OuterUses& uses, Plan& plan)
	{
		// Every enclosing local the body uses and the list does not name is captured the default way; without a
		// default it is an error, as is a `this` the list does not capture.
		std::vector<Core::String> listed;
		for (const PlannedCapture& p : plan) listed.push_back(p.capture.name);
		const CaptureDefault mode = lambda.GetCaptureDefault();
		for (const Core::String& name : uses.names)
		{
			const SymbolInfo* local = m_Ctx.queries().FindLocal(name);
			if (Captured(listed, name) || !local || !local->IsFrameVariable()) continue;
			if (mode == CaptureDefault::None)
				return QueryResult<void>::Fail(QueryError::NotFound, "the lambda uses '" + name + "', a local of the enclosing function, "
					"without capturing it: add it to the capture list, or capture with [=] or [&]");
			auto planned = CaptureVariable(name, mode == CaptureDefault::ByReference);
			if (!planned.Ok()) return QueryResult<void>::Fail(planned.error, planned.message);
			plan.push_back(planned.value);
		}
		if (Captured(listed, kThis) || !(uses.usesThis || UsesMember(uses))) return QueryResult<void>::Success();
		if (mode == CaptureDefault::None || !HasThis())
			return QueryResult<void>::Fail(QueryError::NotFound, HasThis()
				? "the lambda uses 'this' (or a member of '" + m_Ctx.enclosingType->GetName() + "') without capturing it: add 'this' to the capture list"
				: "the lambda uses 'this', but the enclosing function has none");
		auto planned = CaptureThis(false);
		if (!planned.Ok()) return QueryResult<void>::Fail(planned.error, planned.message);
		plan.push_back(planned.value);
		return QueryResult<void>::Success();
	}

	QueryResult<LambdaLowerer::PlannedCapture> LambdaLowerer::CaptureVariable(const Core::String& name, bool byReference)
	{
		using Result = QueryResult<PlannedCapture>;
		const SymbolInfo* local = m_Ctx.queries().FindLocal(name);
		if (!local || !local->IsFrameVariable())
			return Result::Fail(QueryError::NotFound, "'" + name + "' is not a local variable of the enclosing function: only locals "
				"are captured (a global is used directly)");
		const Value variable = m_Ctx.LocalValue(*local);
		PlannedCapture p;
		p.capture.name = name;
		p.capture.type = TypeHandle::UnwrapToValue(variable.type);
		p.capture.kind = byReference ? ClosureCapture::Kind::Reference : ClosureCapture::Kind::Value;
		if (byReference)
		{
			p.capture.constReferent = IsReadOnly(m_Ctx, variable.lvalue);
			const Type* pointer = m_Ctx.queries().TypeStore()->Pointer(p.capture.type, p.capture.constReferent);
			p.init.value = Value::R(m_Ctx.AddressAs(pointer, variable.lvalue), pointer);
		}
		else p.init.value = variable;
		return Result::Success(p);
	}

	QueryResult<LambdaLowerer::PlannedCapture> LambdaLowerer::CaptureInit(const LambdaCapture& capture)
	{
		using Result = QueryResult<PlannedCapture>;
		auto value = m_Expr.ResolveExpr(capture.init.Get());
		if (!value.Ok()) return Result::Fail(value.error, value.message);
		PlannedCapture p;
		p.capture.name = capture.name.GetText();
		p.capture.type = TypeHandle::UnwrapToValue(value.value.type);
		p.capture.kind = capture.byReference ? ClosureCapture::Kind::Reference : ClosureCapture::Kind::Value;
		if (!capture.byReference)
		{
			p.init.value = value.value;
			return Result::Success(p);
		}
		if (!value.value.isLValue)
			return Result::Fail(QueryError::NotAValue, "'&" + p.capture.name + " = ...' must name a location (an lvalue), not a temporary");
		p.capture.constReferent = IsReadOnly(m_Ctx, value.value.lvalue);
		const Type* pointer = m_Ctx.queries().TypeStore()->Pointer(p.capture.type, p.capture.constReferent);
		p.init.value = Value::R(m_Ctx.AddressAs(pointer, value.value.lvalue), pointer);
		return Result::Success(p);
	}

	QueryResult<LambdaLowerer::PlannedCapture> LambdaLowerer::CaptureThis(bool copy)
	{
		using Result = QueryResult<PlannedCapture>;
		if (!HasThis()) return Result::Fail(QueryError::NotFound, "'this' cannot be captured: the enclosing function is not a member function");
		const LValue* object = m_Ctx.ReceiverLValue();
		PlannedCapture p;
		p.capture.name = kThis;
		p.capture.type = m_Ctx.enclosingType;
		p.capture.kind = copy ? ClosureCapture::Kind::ThisCopy : ClosureCapture::Kind::This;
		if (copy) p.init.value = Value::L(object);
		else
		{
			// In a const method the captured `this` points at const.
			p.capture.constReferent = IsReadOnly(m_Ctx, object);
			const Type* pointer = AddressType(m_Ctx, object);
			p.init.value = Value::R(m_Ctx.out->NewNode<AddressOfNode>(pointer, object), pointer);
		}
		return Result::Success(p);
	}

	bool LambdaLowerer::UsesMember(const OuterUses& uses) const
	{
		for (const Core::String& name : uses.names)
		{
			const SymbolInfo* local = m_Ctx.queries().FindLocal(name);
			if ((!local || !local->IsFrameVariable()) && HasInstanceMember(m_Ctx.enclosingType, name)) return true;
		}
		return false;
	}

	bool LambdaLowerer::HasThis() const
	{
		return m_Ctx.enclosingType && (m_Ctx.capturedThis || (m_Ctx.fn && !m_Ctx.fn->isStatic && m_Ctx.fn->containingType));
	}

	QueryResult<void> LambdaLowerer::LowerCallMethod(const Closure& closure)
	{
		// The body lowers now, while the enclosing scopes are pushed: their types, aliases and template
		// bindings are visible in it, their variables are not (only the captures are).
		HIRFunction body = LowerFunction(m_Ctx.model(), m_Ctx.errors(), closure.call);
		if (!body.complete)
		{
			m_Ctx.NoteReported(); // the body reported its own error
			return QueryResult<void>::Fail(QueryError::NotAValue, "the lambda body does not compile");
		}
		m_Ctx.out->nestedFunctions.push_back(std::make_unique<HIRFunction>(std::move(body)));
		return QueryResult<void>::Success();
	}

	// --- the call method's lowering ------------------------------------------------------------------------

	void EnterClosureBody(LoweringContext& ctx, const Closure& closure)
	{
		ctx.enclosingType = closure.ThisCapture() ? closure.enclosingType : nullptr;
		if (closure.call->isStatic) return;
		LValue self; self.kind = LValue::Kind::This; self.type = closure.type;
		ctx.closureObject = ctx.NewLValue(self);
		TypeStore* ts = ctx.queries().TypeStore();
		for (const ClosureCapture& c : closure.captures)
		{
			const ObjectType::FieldInfo& field = closure.type->GetFields()[c.field];
			SymbolInfo sym;
			sym.kind = SymbolKind::Variable;
			switch (c.kind)
			{
			case ClosureCapture::Kind::Value:
				sym.type = c.type;
				ctx.BindCapture(c.name, sym, c.field, false);
				break;
			case ClosureCapture::Kind::Reference:
				sym.type = ts->Pointer(c.type);
				ctx.BindCapture(c.name, sym, c.field, true);
				break;
			case ClosureCapture::Kind::ThisCopy:
			{
				LValue copy; copy.kind = LValue::Kind::Field; copy.type = field.m_Type; copy.base = ctx.closureObject; copy.field = &field;
				ctx.capturedThis = ctx.NewLValue(copy);
				break;
			}
			case ClosureCapture::Kind::This:
			{
				LValue pointer; pointer.kind = LValue::Kind::Field; pointer.type = field.m_Type; pointer.base = ctx.closureObject; pointer.field = &field;
				LValue object; object.kind = LValue::Kind::Deref; object.type = closure.enclosingType;
				object.addr = ctx.LoadIfNeeded(Value::L(ctx.NewLValue(pointer)));
				ctx.capturedThis = ctx.NewLValue(object);
				break;
			}
			}
		}
	}
}
