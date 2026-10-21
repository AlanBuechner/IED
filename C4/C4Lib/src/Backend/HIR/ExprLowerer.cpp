#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/ReferenceBinding.h"
#include <tuple>
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Backend/HIR/LambdaLowerer.h"
#include "Backend/HIR/Callables.h"
#include "Backend/HIR/FoldLowerer.h"
#include "Backend/HIR/PackElements.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/VarArgs.h"
#include "Parser/Nodes/Expressions/VaArgExpressionNode.h"
#include "Backend/HIR/BuiltinOperators.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/ConditionalBranches.h"
#include "Model/Types/ClassHierarchy.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/UnaryExpressionNode.h"
#include "Parser/Nodes/Expressions/AssignmentExpressionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include "Parser/Nodes/Expressions/TernaryExpressionNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/ArrayIndexNode.h"
#include "Parser/Nodes/Expressions/TypeCheckNode.h"
#include "Parser/Nodes/Expressions/SafeCastNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/NewExpressionNode.h"
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h" // sizeof(T) / __uuidof(T)
#include "Parser/Nodes/Expressions/BracedInitializerNode.h" // init-list call arguments
#include "Parser/Nodes/Expressions/LambdaNode.h"            // lambda expressions (anonymous local functions)
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include "Parser/Nodes/Expressions/ValueListNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Model/Types/ReturnLists.h"
#include "Model/Types/QualifiedName.h"
#include "Backend/HIR/PackedViews.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ArrayType.h"

namespace Scripting
{
	QueryResult<Value> ExprLowerer::ResolveExpr(const AstNode* node)
	{
		if (!node) return QueryResult<Value>::Fail(QueryError::NotAValue, "empty expression");
		switch (node->Type()) {
		case NodeType::LiteralExpression:       return ResolveLiteral(dynamic_cast<const LiteralNode*>(node));
		case NodeType::IdentifierExpression:    return ResolveIdentifier(dynamic_cast<const IdentifierExpressionNode*>(node));
		case NodeType::MemberAccess:            return ResolveMemberAccess(dynamic_cast<const MemberAccessNode*>(node));
		case NodeType::BinaryExpression:        return ResolveBinary(dynamic_cast<const BinaryExpressionNode*>(node));
		case NodeType::ConditionalAnd:          return ResolveShortCircuit(node, true);
		case NodeType::ConditionalOr:           return ResolveShortCircuit(node, false);
		case NodeType::UnaryExpression:         return ResolveUnary(dynamic_cast<const UnaryExpressionNode*>(node));
		case NodeType::AssignmentExpression:    return ResolveAssignment(dynamic_cast<const AssignmentExpressionNode*>(node));
		case NodeType::FunctionCall:            return ResolveCall(dynamic_cast<const FunctionCallNode*>(node));
		case NodeType::CastExpression:          return ResolveCast(dynamic_cast<const CastExpressionNode*>(node));
		case NodeType::TernaryExpression:       return ResolveTernary(dynamic_cast<const TernaryExpressionNode*>(node));
		case NodeType::ParenthesizedExpression: return ResolveParen(dynamic_cast<const ParenthesizedExpressionNode*>(node));
		case NodeType::ArrayIndex:              return ResolveArrayIndex(dynamic_cast<const ArrayIndexNode*>(node));
		case NodeType::TypeCheck:               return ResolveTypeCheck(dynamic_cast<const TypeCheckNode*>(node));
		case NodeType::SafeCast:                return ResolveSafeCast(dynamic_cast<const SafeCastNode*>(node));
		case NodeType::StaticAccess:            return ResolveStaticAccess(dynamic_cast<const StaticAccessNode*>(node));
		case NodeType::NamespaceQualifiedName:  return ResolveInstanceName(dynamic_cast<const NamespaceQualifiedNameNode*>(node));
		case NodeType::NewExpression:           return ResolveNew(dynamic_cast<const NewExpressionNode*>(node));
		case NodeType::TypeOfExpression:        return ResolveTypeOf(dynamic_cast<const TypeOfExpressionNode*>(node));
		case NodeType::VaArgExpression:         return VarArgs::LowerArgument(m_Ctx, *this, *static_cast<const VaArgExpressionNode*>(node));
		case NodeType::LambdaExpression:        return ResolveLambda(dynamic_cast<const LambdaNode*>(node));
		case NodeType::FoldExpression:          return FoldLowerer(m_Ctx, *this).Lower(*dynamic_cast<const FoldExpressionNode*>(node));
		case NodeType::ValueList:
			return m_Ctx.Fail(node, QueryResult<Value>::Fail(QueryError::NotAValue,
				"a value list '" + node->ToString() + "' is only returned from a function that returns a list, or destructured"));
		case NodeType::PackExpansion:
			return m_Ctx.Fail(node, QueryResult<Value>::Fail(QueryError::NotAValue,
				"'...' expands a pack only in the arguments of a call, the entries of a braced list or a template argument list"));
		case NodeType::BracedInitializer:
			return m_Ctx.Fail(node, QueryResult<Value>::Fail(QueryError::NotAValue,
				"the braced list '" + node->ToString() + "' has no type here: write the type it builds, T{...}, or initialize a variable"));
		default:
			return m_Ctx.Fail(node, QueryResult<Value>::Fail(QueryError::Unsupported, "the expression '" + node->ToString() + "' is not supported here"));
		}
	}

	namespace
	{
		// While a default argument lowers: a fresh frame hides the caller's locals, names resolve from the
		// callee's own scope and unit, and there is no receiver (a default cannot name `this` or a field).
		class DeclarationSite
		{
		public:
			DeclarationSite(LoweringContext& ctx, const FunctionRecord& callee)
				: m_Ctx(ctx), m_Saved(ctx.scope, ctx.unitId, ctx.enclosingType, ctx.primitiveThis, ctx.closureObject, ctx.capturedThis),
				  m_Frame(ctx.queries().PushScope(Scope::Kind::Function, "default-argument"))
			{
				ctx.scope = callee.scope;
				ctx.unitId = callee.declUnit;
				ctx.enclosingType = nullptr;
				ctx.primitiveThis = nullptr;
				ctx.closureObject = nullptr;
				ctx.capturedThis = nullptr;
			}
			~DeclarationSite()
			{
				std::tie(m_Ctx.scope, m_Ctx.unitId, m_Ctx.enclosingType, m_Ctx.primitiveThis, m_Ctx.closureObject, m_Ctx.capturedThis) = m_Saved;
			}

		private:
			LoweringContext& m_Ctx;
			std::tuple<const Scope*, uint32, const ObjectType*, const Type*, const LValue*, const LValue*> m_Saved;
			ScopeGuard m_Frame;
		};
	}

	QueryResult<Value> ExprLowerer::DefaultArgument(const FunctionRecord& callee, uint32 index)
	{
		DeclarationSite site(m_Ctx, callee);
		return ResolveExpr(callee.paramDefaults[index]);
	}

	QueryResult<Value> ExprLowerer::ResolveIdentifier(const IdentifierExpressionNode* n)
	{
		const Core::String& name = n->GetName();
		if (name == "this")
		{
			// An object receiver's this is the object location; a PRIMITIVE receiver's this (extension
			// method `extends float`) is a POINTER to the value, so `*this` loads it. Both are This lvalues;
			// the type tells codegen what lives at the address.
			if (m_Ctx.capturedThis) return Value::L(m_Ctx.capturedThis);
			if (m_Ctx.InStaticMethod()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, m_Ctx.NoObject("'this' is not available")));
			const Type* thisType = m_Ctx.enclosingType ? static_cast<const Type*>(m_Ctx.enclosingType) : m_Ctx.primitiveThis;
			if (!thisType) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, m_Ctx.closureObject
				? "'this' is not captured by this lambda: add 'this' to its capture list" : "'this' used outside a member function"));
			LValue lv; lv.kind = LValue::Kind::This; lv.type = thisType;
			return Value::L(m_Ctx.NewLValue(lv));
		}

		if (Packs::Find(m_Ctx.model(), name, nullptr))
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "the pack '" + name + "' must be expanded with '...'"));
		auto q = m_Ctx.queries().QueryVariable(name, nullptr, m_Ctx.unitId, m_Ctx.enclosingType, m_Ctx.scope);
		if (!q.Ok())
		{
			// A packed view of the enclosing type (bare `x`, `xy` or `data` inside a vector method): QueryVariable
			// only knows ordinary fields, so a view fails there. Checked before function decay.
			if (m_Ctx.enclosingType)
			{
				const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(m_Ctx.enclosingType));
				if (const LValue* view = PackedViewLValue(m_Ctx, obj, m_Ctx.ReceiverLValue(), name)) return Value::L(view);
			}

			// Function-to-pointer decay: a function name used in expression position (`f = Add`,
			// `Apply(Add, 1, 2)`, `Type::Make`) is the function's address as a funcptr of its exact signature.
			// A free function or a static method; an instance method has no address without an object.
			if (const auto* enclosing = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(m_Ctx.enclosingType)))
				if (auto method = MethodValue(n, enclosing, name); method.Ok() || method.error != QueryError::NotFound) return method;
			if (auto function = FunctionValue(n, name); function.Ok() || function.error != QueryError::NotFound) return function;
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(q.error, q.message));
		}
		const VariableInfo& v = q.value;

		// A compile-time constant with no addressable storage (a bound non-type template parameter):
		// it is registered in the lowering scope but has NO slot - emit its folded value directly.
		// Checked before the local-slot path, which would otherwise build a slot lvalue from a name
		// that was never given one. The type follows the value kind, as integer literals do (int32).
		if (!v.type && v.isConst && !v.constValue.IsNone())
		{
			TypeStore* ts = m_Ctx.queries().TypeStore();
			const Type* ct = (v.constValue.kind == ConstantValue::Kind::Float64) ? ts->Builtin(TypeKind::Float64)
				: (v.constValue.kind == ConstantValue::Kind::Bool) ? ts->Builtin(TypeKind::Bool)
				: ts->Builtin(TypeKind::Int32);
			return Value::R(m_Ctx.out->NewNode<ConstantNode>(ct, v.constValue), ct);
		}

		// A local (parameter or body local): the innermost binding of the name carries its slot.
		if (const SymbolInfo* local = m_Ctx.queries().FindLocal(name))
			if (local->IsFrameVariable())
				return m_Ctx.LocalValue(*local);

		// A member of the enclosing type (field): an lvalue at the implicit receiver, through the sub-object
		// that declares it. A static data member is one shared global in its declaring type's scope.
		if (m_Ctx.enclosingType)
		{
			ClassHierarchy::FieldHit hit = ClassHierarchy::FindField(m_Ctx.enclosingType, name);
			if (hit.field)
				if (auto allowed = AccessCheck::CheckField(m_Ctx, hit.owner, *hit.field); !allowed.Ok())
					return m_Ctx.Fail(n, QueryResult<Value>::Fail(allowed.error, allowed.message));
			if (hit.field && hit.field->m_IsStatic)
				if (const SymbolInfo* sym = StaticMemberSymbol(hit.owner, name))
				{
					LValue lv; lv.kind = LValue::Kind::Global; lv.type = hit.field->m_Type; lv.sym = sym;
					return Value::L(m_Ctx.NewLValue(lv));
				}
			if (hit.field)
			{
				if (m_Ctx.InStaticMethod())
					return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, m_Ctx.NoObject("'" + name + "' is a field")));
				if (hit.ambiguous)
					return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::Ambiguous, "'" + name + "' is ambiguous: more than one base of '" + m_Ctx.enclosingType->GetName() + "' declares it"));
				LValue lv; lv.kind = LValue::Kind::Field; lv.type = hit.field->m_Type; lv.field = hit.field;
				lv.base = SubobjectLValue(m_Ctx, m_Ctx.ReceiverLValue(), hit.owner, hit.ownerOffset);
				const LValue* field = m_Ctx.NewLValue(lv);
				return Value::L(ReferenceBinding::Of(hit.field->m_Type) ? ReferenceBinding::Referent(m_Ctx, field) : field);
			}
		}

		// A compile-time constant whose folded value is not stored on the symbol (an unscoped enum member
		// registers as a const variable whose value lives on the EnumType): resolve it through QueryConstant
		// and emit a literal, NOT a Global lvalue (whose backing storage would be zero-initialized -> reads 0).
		if (v.symbol && v.symbol->isConst && v.symbol->constValue.IsNone())
		{
			auto cv = m_Ctx.queries().QueryConstant(name, m_Ctx.unitId, m_Ctx.scope);
			if (cv.Ok() && !cv.value.IsNone())
				return Value::R(m_Ctx.out->NewNode<ConstantNode>(v.type, cv.value), v.type);
			// An enum declared in the body is in no namespace QueryConstant searches; its type has the value.
			if (const auto* en = dynamic_cast<const EnumType*>(TypeHandle::UnwrapToValue(v.type)))
				if (const EnumType::MemberInfo* member = en->GetMemberByName(name))
					return Value::R(m_Ctx.out->NewNode<ConstantNode>(v.type, ConstantValue::Int(member->m_Value)), v.type);
		}

		// A global (or static): addressable through its model symbol.
		if (v.symbol)
		{
			LValue lv; lv.kind = LValue::Kind::Global; lv.type = v.type; lv.sym = v.symbol;
			return Value::L(m_Ctx.NewLValue(lv));
		}

		return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotCallable, "name is not a variable: " + name));
	}

	QueryResult<Value> ExprLowerer::FunctionValue(const AstNode* n, const Core::String& name, const TemplateArgumentListNode* tmplArgs)
	{
		const FunctionRecord* rec = nullptr;
		auto sig = m_Ctx.queries().QueryFunction(name, nullptr, tmplArgs, Core::Vector<const Type*>(), m_Ctx.unitId, m_Ctx.enclosingType, m_Ctx.scope, &rec);
		if (sig.Ok() && rec) // the specific function named - NOT RecordFor(sig), which is ambiguous for same-signature functions
			return DecayedFunction(n, name, *rec, sig.value);
		if (tmplArgs) return m_Ctx.Fail(n, QueryResult<Value>::Fail(sig.error, sig.message));
		if (m_Ctx.queries().QueryTemplateName(name, m_Ctx.unitId, m_Ctx.scope))
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "'" + name + "' is a function template: write its template "
				"arguments (" + name + "<...>), or use it where a funcptr or func type says which instance it is"));
		return QueryResult<Value>::Fail(QueryError::NotFound, "no function named " + name);
	}

	QueryResult<Value> ExprLowerer::ResolveInstanceName(const NamespaceQualifiedNameNode* n)
	{
		const auto& segments = n->GetSegments();
		Core::String name;
		for (const auto& segment : segments) name += (name.empty() ? "" : "::") + segment.nameToken.GetText();
		const TemplateArgumentListNode* args = segments.Back().templateArgs.Get();
		for (uint32 i = 0; i + 1 < segments.Count(); ++i)
			if (segments[i].templateArgs) args = nullptr; // `Box<int32>::...` is a type's member, not a function's instance
		if (!args || !m_Ctx.queries().QueryTemplateName(name, m_Ctx.unitId, m_Ctx.scope))
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "'" + n->ToString() + "' is not a value: only a function "
				"template's instance is (a type builds one with '{...}' or '(...)')"));
		return FunctionValue(n, name, args);
	}

	QueryResult<Value> ExprLowerer::DecayedFunction(const AstNode* n, const Core::String& name, const FunctionRecord& rec, const FunctionType* sig)
	{
		if (rec.containingType && !rec.isStatic)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "'" + name + "' is a method: it has no address without an object; "
				"pass a lambda that calls it on one"));
		if (auto allowed = AccessCheck::CheckCall(m_Ctx, rec); !allowed.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(allowed.error, allowed.message));
		Core::Vector<const Type*> ptypes;
		for (const auto& p : sig->GetParameters()) ptypes.Push(p.m_Type);
		const Type* fp = m_Ctx.queries().TypeStore()->FunPtr(sig->GetReturnType(), ptypes);
		return Value::R(m_Ctx.out->NewNode<FunctionAddressNode>(fp, &rec), fp);
	}

	QueryResult<Value> ExprLowerer::ResolveFor(const AstNode* node, const Type* target)
	{
		if (const auto* list = dynamic_cast<const ValueListNode*>(node))
		{
			const auto* returned = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(target));
			if (!returned || !returned->IsReturnList())
				return m_Ctx.Fail(node, QueryResult<Value>::Fail(QueryError::NotAValue, "a value list starts only a return list, not '" +
					(target ? TypeHandle::UnwrapToValue(target)->GetName() : Core::String("?")) + "'"));
			return ResolveValueList(list, returned);
		}
		const auto* signature = dynamic_cast<const SignatureType*>(TypeHandle::UnwrapToValue(target));
		if (const TemplateNameType* named = signature ? TemplateNameOf(node) : nullptr) return TemplateAddress(node, *named, *signature);
		// `p = { 3, 4 }`, `arr = { 1, 2, 3 }`: the braces build a value of the target's type.
		if (const auto* brace = dynamic_cast<const BracedInitializerNode*>(node); brace && target) return ResolveBracedAs(brace, target);
		const auto* text = dynamic_cast<const LiteralNode*>(node);
		const auto* array = dynamic_cast<const ArrayType*>(TypeHandle::UnwrapToValue(target));
		if (text && array && text->GetKind() == LiteralNode::LiteralKind::String) return ResolveTextAs(text, array);
		return ResolveExpr(node);
	}

	QueryResult<std::vector<Value>> ExprLowerer::ResolveArgument(const AstNode* node)
	{
		if (const auto* expansion = dynamic_cast<const PackExpansionNode*>(node)) return ResolveExpansion(expansion);
		auto value = ResolveExpr(node);
		if (!value.Ok()) return QueryResult<std::vector<Value>>::Fail(value.error, value.message);
		return std::vector<Value>{ value.value };
	}

	QueryResult<std::vector<Value>> ExprLowerer::ResolveExpansion(const PackExpansionNode* n)
	{
		using Result = QueryResult<std::vector<Value>>;
		if (dynamic_cast<const BracedInitializerNode*>(n->GetPattern().Get()))
			return m_Ctx.Fail(n, Result::Fail(QueryError::Unsupported,
				"a braced list is not expanded in arguments: expand the pack inside it, as in {args...}"));
		auto copies = PackElements::Of(m_Ctx, *n->GetPattern());
		if (!copies.Ok()) return Result::Fail(copies.error, copies.message);
		std::vector<Value> values;
		auto each = copies.value.ForEach([&](uint32) {
			auto value = ResolveExpr(copies.value.Pattern());
			if (!value.Ok()) return QueryResult<void>::Fail(value.error, value.message);
			values.push_back(value.value);
			return QueryResult<void>::Success();
		});
		if (!each.Ok()) return Result::Fail(each.error, each.message);
		return values;
	}

	QueryResult<Value> ExprLowerer::ResolveTernary(const TernaryExpressionNode* n)
	{
		auto cond = ResolveExpr(n->GetCondition().Get());
		if (!cond.Ok()) return cond;
		if (Core::String why = ImplicitConversion::ConditionProblem(m_Ctx, cond.value.type); !why.empty())
			return m_Ctx.Fail(n->GetCondition().Get(), QueryResult<Value>::Fail(QueryError::NotAValue, why));
		auto thenV = ResolveExpr(n->GetThenBranch().Get());
		if (!thenV.Ok()) return thenV;
		auto elseV = ResolveExpr(n->GetElseBranch().Get());
		if (!elseV.Ok()) return elseV;
		// `own ? this : other`: beside a pointer, `this` is the receiver's address.
		auto isPointer = [](const Value& v) { const Type* t = TypeHandle::UnwrapToValue(v.type); return t && t->GetKind() == TypeKind::Pointer; };
		if (isPointer(elseV.value)) thenV.value = ThisAsPointer(thenV.value);
		if (isPointer(thenV.value)) elseV.value = ThisAsPointer(elseV.value);

		auto common = ConditionalBranches::CommonType(m_Ctx, thenV.value, elseV.value);
		if (!common.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(common.error, common.message));
		auto thenN = ConditionalBranches::BranchValue(m_Ctx, thenV.value, common.value);
		if (!thenN.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(thenN.error, thenN.message));
		auto elseN = ConditionalBranches::BranchValue(m_Ctx, elseV.value, common.value);
		if (!elseN.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(elseN.error, elseN.message));
		const Type* t = common.value;
		return Value::R(m_Ctx.out->NewNode<TernaryNode>(t, m_Ctx.LoadIfNeeded(cond.value), thenN.value, elseN.value), t);
	}

	QueryResult<Value> ExprLowerer::ResolveParen(const ParenthesizedExpressionNode* n)
	{
		return ResolveExpr(n->GetExpression().Get()); // transparent: grouping only
	}

	QueryResult<Value> ExprLowerer::ResolveLambda(const LambdaNode* n)
	{
		return LambdaLowerer(m_Ctx, *this).Lower(*n);
	}
} // namespace Scripting
