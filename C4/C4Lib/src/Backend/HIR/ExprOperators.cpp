#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/CallResults.h"
#include "Backend/HIR/ConstantOperands.h"
#include <tuple>
#include "Backend/HIR/EvaluatedOnce.h"
#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/PointerArithmetic.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Backend/HIR/LambdaLowerer.h"
#include "Backend/HIR/Callables.h"
#include "Backend/HIR/FoldLowerer.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/VarArgs.h"
#include "Backend/HIR/BuiltinOperators.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Parser/Nodes/Expressions/ConditionalAndNode.h"
#include "Parser/Nodes/Expressions/ConditionalOrNode.h"
#include "Parser/Nodes/Expressions/UnaryExpressionNode.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h" // init-list call arguments
#include "Model/Types/ReturnLists.h"
#include "Model/Types/QualifiedName.h"
#include "Model/TypeHandle.h"
#include <algorithm>
#include <iterator>
#include <optional>

namespace Scripting
{
	QueryResult<Value> ExprLowerer::ResolveBinary(const BinaryExpressionNode* n)
	{
		const Core::String tok = n->GetOperatorToken().GetText();

		// Compound literal `T{a, b}`: the parser chains it as a BinaryExpression (left=type name,
		// right=BracedInitializer). It is NOT an operator - resolve it as an aggregate construction.
		if (const auto* brace = NodeAs<BracedInitializerNode>(n->GetRightOperand().Get()))
		{
			const AstNode* written = n->GetLeftOperand().Get();
			auto type = QualifierType(written);
			if (!type.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(type.error, type.message));
			if (const auto* obj = TypeAs<ObjectType>(type.value)) return ResolveBracedObject(written, obj, brace);
		}

		if (const Core::String misread = TypeAsOperand(n, tok); !misread.empty())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, misread));
		auto lhs = ResolveExpr(n->GetLeftOperand().Get());
		if (!lhs.Ok()) return lhs;
		auto rhs = ResolveExpr(n->GetRightOperand().Get());
		if (!rhs.Ok()) return rhs;
		return ApplyBinary(n, tok, lhs.value, rhs.value);
	}

	Core::String ExprLowerer::TypeAsOperand(const BinaryExpressionNode* n, const Core::String& tok) const
	{
		static constexpr const char* kAlsoPrefix[] = { "+", "-", "*", "**", "&", "&&" };
		if (std::find(std::begin(kAlsoPrefix), std::end(kAlsoPrefix), tok) == std::end(kAlsoPrefix)) return "";
		const auto* paren = dynamic_cast<const ParenthesizedExpressionNode*>(n->GetLeftOperand().Get());
		const AstNode* inner = paren ? paren->GetExpression().Get() : nullptr;
		if (!inner) return "";
		if (const auto* id = NodeAs<IdentifierExpressionNode>(inner);
			id && m_Ctx.queries().QueryVariable(id->GetName(), nullptr, m_Ctx.unitId, m_Ctx.enclosingType, m_Ctx.scope).Ok())
			return "";
		auto type = QualifierType(inner);
		if (!type.Ok() || !type.value) return "";
		const Core::String name = inner->ToString();
		return "'(" + name + ") " + tok + "' is not a cast: a name in parentheses before '" + tok + "' is its left operand; write '(" +
			name + ")(" + tok + "x)' to cast";
	}

	QueryResult<Value> ExprLowerer::ApplyBinary(const AstNode* n, const Core::String& tok, Value lhs, Value rhs)
	{
		// HIR contract: a resolved value always carries a concrete type. If one is null, something
		// upstream produced an untyped value - report it cleanly instead of dereferencing it below.
		if (!lhs.type || !rhs.type)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "operator '" + tok + "' has an operand with no type"));
		lhs = ThisAsPointer(lhs);
		rhs = ThisAsPointer(rhs);
		// `f == null` / `f != null`: a func compares by its code pointer.
		if (tok == "==" || tok == "!=")
		{
			lhs = FuncCode(m_Ctx, lhs);
			rhs = FuncCode(m_Ctx, rhs);
		}

		const Type* l = TypeHandle::UnwrapToValue(lhs.type);
		const Type* r = TypeHandle::UnwrapToValue(rhs.type);

		// null (void*) vs a pointer/funcptr: `f == null` / `f != null`. Exactly one side is the void*
		// null and the other is some pointer-like type; lift the null to the other's type so the
		// intrinsic sees matching operand types (the same coercion as assignment).
		if ((tok == "==" || tok == "!="))
		{
			TypeStore* ts = m_Ctx.queries().TypeStore();
			const Type* voidPtr = ts->Pointer(ts->Builtin(TypeKind::Void));
			auto pointerLike = [](const Type* t) { return t && (t->GetKind() == TypeKind::Pointer || t->GetKind() == TypeKind::FunPtr); };
			if ((l == voidPtr && pointerLike(r)) || (r == voidPtr && pointerLike(l)))
			{
				const bool lVoid = (l == voidPtr);
				const Type* target = lVoid ? r : l;
				Value& side = lVoid ? lhs : rhs;
				const Type* fromT = lVoid ? l : r;
				side = Value::R(m_Ctx.out->NewNode<ConvertNode>(m_Ctx.LoadIfNeeded(side), fromT, target), target);
				if (lVoid) l = target; else r = target; // keep the locals in sync for the intrinsic below
			}
		}

		// An operator method on either object type wins (doc 08: one query, one answer).
		auto op = m_Ctx.queries().QueryOperator(tok, l, r, m_Ctx.unitId, m_Ctx.enclosingType, AccessOf(lhs), AccessOf(rhs));
		if (op.Ok() && op.value.record) return EmitOperatorCall(n, op.value, lhs, rhs);
		if (op.error == QueryError::ReadOnly) return m_Ctx.Fail(n, QueryResult<Value>::Fail(op.error, op.message));

		// Primitives: the built-in operation. Operands of different numeric kinds are first converted
		// to their common type (the model's usual arithmetic conversions) so the intrinsic sees matching
		// operand types - `list.size == 3` is a uint64 vs int32 compare, not a raw mismatched one.
		const IntrinsicOp* ik = ModelQueries::IntrinsicForBinary(tok);
		if (ik)
		{
			bool isCompare = tok == "==" || tok == "!=" || tok == "<" || tok == ">" || tok == "<=" || tok == ">=";
			const Type* common = l;

			// A pure packed vector object beside a run of its lanes is that vector (`v3.xy += v2`).
			if (std::optional<Value> lanes = SameLanesAs(lhs, r)) { lhs = *lanes; l = TypeHandle::UnwrapToValue(lhs.type); }
			if (std::optional<Value> lanes = SameLanesAs(rhs, l)) { rhs = *lanes; r = TypeHandle::UnwrapToValue(rhs.type); }

			// An object or union has no built-in operator: without a method on the left operand's type it is an error.
			auto aggregate = [](const Type* t) { return t->GetKind() == TypeKind::Object || t->GetKind() == TypeKind::Union; };
			if (aggregate(l) || aggregate(r))
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotFound, BuiltinOperators::MissingOperator(tok, l, r)));

			// Pointer arithmetic (Backend/HIR/PointerArithmetic): an address offset or a pointer difference.
			if (IsPointerArithmetic(tok, l, r))
				return m_Ctx.Fail(n, PointerArithmetic(m_Ctx, tok, lhs, rhs));

			// Pointer compared to an integer (`p == 0` / `p != 0`, and funcptr/string forms): C4 only
			// allows a pointer-like value against the integer zero, which means null. Lift the integral
			// operand to the pointer side's type so the compare sees matching operand types - the same
			// coercion as the void*-null case above, generalized to every pointer-like kind (pointer,
			// funcptr, string) instead of only TypeKind::Pointer.
			auto pointerLike = [](const Type* t) {
				return t && (t->GetKind() == TypeKind::Pointer || t->GetKind() == TypeKind::FunPtr); };
			if (isCompare && (tok == "==" || tok == "!="))
			{
				auto isInt = [](const Value& v) { const Type* t = TypeHandle::UnwrapToValue(v.type); return t && t->IsIntegral(); };
				if (pointerLike(l) && isInt(rhs))
					rhs = Value::R(m_Ctx.out->NewNode<ConvertNode>(m_Ctx.LoadIfNeeded(rhs), rhs.type, l), l);
				else if (pointerLike(r) && isInt(lhs))
					lhs = Value::R(m_Ctx.out->NewNode<ConvertNode>(m_Ctx.LoadIfNeeded(lhs), lhs.type, r), r);
			}
			if (Core::String why = BuiltinOperators::BinaryProblem(tok, TypeHandle::UnwrapToValue(lhs.type), TypeHandle::UnwrapToValue(rhs.type));
				!why.empty())
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, why));
			if (l->GetKind() == TypeKind::Swizzle || r->GetKind() == TypeKind::Swizzle) return VectorBinary(n, *ik, isCompare, lhs, rhs);

			// Usual arithmetic conversions apply even for SAME-TYPE narrow operands: int16 + int16 promotes
			// both to int32 (C integer promotions), so the op runs at >=32 bits and cannot wrap in 16. When
			// the common type equals an operand's type no conversion is inserted (the `common != l` guard).
			if (l->IsNumeric() && r->IsNumeric())
			{
				TypeKind ck = Type::UsualArithmeticResultKind(l->GetKind(), r->GetKind());
				if (ck != TypeKind::Invalid) common = m_Ctx.queries().TypeStore()->Builtin(ck);
			}
			const HIRNode* lNode = m_Ctx.LoadIfNeeded(lhs);
			const HIRNode* rNode = m_Ctx.LoadIfNeeded(rhs);
			if (common != l && lNode->type != common) lNode = m_Ctx.out->NewNode<ConvertNode>(lNode, lNode->type, common);
			if (common != r && rNode->type != common) rNode = m_Ctx.out->NewNode<ConvertNode>(rNode, rNode->type, common);
			if (Core::String why = ConstantOperands::BinaryProblem(*ik, common, rNode); !why.empty())
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, why));
			const Type* t = isCompare ? m_Ctx.queries().TypeStore()->Builtin(TypeKind::Bool) : common;
			return Value::R(m_Ctx.out->NewNode<IntrinsicOpNode>(t, *ik, lNode, rNode), t);
		}
		return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotFound, "no operator '" + tok + "' for these operands"));
	}

	QueryResult<Value> ExprLowerer::ResolveShortCircuit(const AstNode* n, bool isAnd)
	{
		// The parser yields dedicated node types for short-circuit logical operators (they are NOT
		// BinaryExpressionNodes): ConditionalAndNode / ConditionalOrNode. Both expose the same
		// operand accessors, so one handler covers both.
		const AstNode* leftNode = nullptr;
		const AstNode* rightNode = nullptr;
		if (const auto* ca = dynamic_cast<const ConditionalAndNode*>(n)) { leftNode = ca->GetLeftOperand().Get(); rightNode = ca->GetRightOperand().Get(); }
		else if (const auto* co = dynamic_cast<const ConditionalOrNode*>(n)) { leftNode = co->GetLeftOperand().Get(); rightNode = co->GetRightOperand().Get(); }
		if (!leftNode) return QueryResult<Value>::Fail(QueryError::NotAValue, "short-circuit operator expected");

		auto lhs = ResolveExpr(leftNode);
		if (!lhs.Ok()) return lhs;
		auto rhs = ResolveExpr(rightNode); // type-checked now, EVALUATED only when needed
		if (!rhs.Ok()) return rhs;
		return ApplyShortCircuit(n, isAnd, lhs.value, rhs.value);
	}

	QueryResult<Value> ExprLowerer::ApplyShortCircuit(const AstNode* n, bool isAnd, const Value& lhs, const Value& rhs)
	{
		const Type* boolT = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Bool);

		// Object operands have NO built-in truth value (TypeHandle::HasTruthValue is false for objects):
		// `a && b` / `a || b` on structs dispatch to [operator("logical_and")] / [operator("logical_or")]
		// on the left operand's type, exactly like any other binary operator (doc 07 Bug D). Primitives keep
		// the short-circuit ternary below.
		const Type* l = TypeHandle::UnwrapToValue(lhs.type);
		const Type* r = TypeHandle::UnwrapToValue(rhs.type);
		if (TypeAs<ObjectType>(l) || TypeAs<ObjectType>(r))
		{
			const char* tok = isAnd ? "&&" : "||";
			auto op = m_Ctx.queries().QueryOperator(tok, l, r, m_Ctx.unitId, m_Ctx.enclosingType, AccessOf(lhs), AccessOf(rhs));
			if (op.Ok() && op.value.record) return EmitOperatorCall(n, op.value, lhs, rhs);
			if (op.error == QueryError::ReadOnly) return m_Ctx.Fail(n, QueryResult<Value>::Fail(op.error, op.message));
		}
		for (const Value* operand : { &lhs, &rhs })
			if (Core::String why = ImplicitConversion::ConditionProblem(m_Ctx, operand->type); !why.empty())
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue,
					Core::String("'") + (isAnd ? "&&" : "||") + "' joins two conditions: " + why));

		// c && b -> c ? <b> : false ;  c || b -> c ? true : <b>. The ternary's region structure is
		// what makes the unchosen side not run - short-circuiting falls out of it (05, note).
		const HIRNode* constC = m_Ctx.out->NewNode<ConstantNode>(boolT, ConstantValue::Bool(!isAnd));
		// The ternary is BOOL-typed, so BOTH arms must be i1. The chosen side is the raw rhs value; when
		// that operand is not already a bool (e.g. `count && flag` with count:int32) it must be coerced by
		// truthiness to match the constant arm - otherwise the PHI mixes i32 and i1 and LLVM asserts. The
		// condition (lhs) is coerced separately by AsBool at emit time, so only the value arm needs this.
		// C4 lets any HasTruthValue operand appear in && / || (int/float/pointer are all truthy), matching
		// if/while; reusing BoolNot twice (double negation) reuses the existing per-type truthiness logic
		// exactly instead of duplicating AsBool at HIR level.
		auto coerceToBool = [&](const HIRNode* x) -> const HIRNode* {
			if (x->type && x->type->GetKind() == TypeKind::Bool) return x;
			const HIRNode* not1 = m_Ctx.out->NewNode<IntrinsicOpNode>(boolT, IntrinsicOp::BoolNot, x, nullptr);
			return m_Ctx.out->NewNode<IntrinsicOpNode>(boolT, IntrinsicOp::BoolNot, not1, nullptr);
		};
		const HIRNode* rhsArm = coerceToBool(m_Ctx.LoadIfNeeded(rhs));
		const HIRNode* chosen = isAnd ? rhsArm : constC;
		const HIRNode* other = isAnd ? constC : rhsArm;
		return Value::R(m_Ctx.out->NewNode<TernaryNode>(boolT, m_Ctx.LoadIfNeeded(lhs), chosen, other), boolT);
	}

	QueryResult<Value> ExprLowerer::EmitOperatorCall(const AstNode* n, const OperatorBinding& b, const Value& lhs, const Value& rhs)
	{
		// The left operand owns the method (D-25); a unary operator's operand is `lhs`.
		const FunctionRecord* rec = b.record;
		if (auto allowed = AccessCheck::CheckCall(m_Ctx, *rec); !allowed.Ok())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(allowed.error, allowed.message));

		// A by-value receiver (`GetVec() + other`, `2.0f * v`) is materialized (or made a temporary) so `this`
		// has an address.
		const LValue* receiverL = Lifecycle(m_Ctx).AddressableObject(lhs, TypeHandle::UnwrapToValue(lhs.type));
		auto call = m_Ctx.out->NewNode<CallNode>(rec->type->GetReturnType(), rec);
		call->isOperatorCall = true;
		if (auto bound = BindReceiver(m_Ctx, call, receiverL ? receiverL : m_Ctx.NewLValue(LValue())); !bound.Ok())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(bound.error, bound.message));
		// A UNARY operator method (unary_plus, negate...) has no explicit parameter: the receiver IS the
		// operand. Binary ones pass the other operand.
		if (rec->type && rec->type->GetParameterCount() > 0)
		{
			auto arg = BindArgument(m_Ctx, rhs, m_Ctx.LoadIfNeeded(rhs), rec->type->GetParameters()[0].m_Type);
			if (!arg.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(arg.error, arg.message));
			call->args.push_back(arg.value);
		}
		return CallResult(m_Ctx, call, rec->type->GetReturnType());
	}

	QueryResult<Value> ExprLowerer::ResolveUnary(const UnaryExpressionNode* n)
	{
		const Core::String tok = n->GetOperatorToken().GetText();
		auto operand = ResolveExpr(n->GetOperand().Get());
		if (!operand.Ok()) return operand;
		if (tok == "!") operand.value = FuncCode(m_Ctx, operand.value); // `!f`: a func is null when its code is
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* t = TypeHandle::UnwrapToValue(operand.value.type);

		// `*this` is the object behind the this-pointer, never a call of the type's own dereference operator.
		if (tok == "*" && operand.value.isLValue && IsThisObject(operand.value.lvalue))
		{
			LValue object; object.kind = LValue::Kind::Deref; object.type = operand.value.lvalue->type;
			object.addr = m_Ctx.out->NewNode<AddressOfNode>(AddressType(m_Ctx, operand.value.lvalue), operand.value.lvalue);
			return Value::L(m_Ctx.NewLValue(object));
		}

		// An operator method on an object type wins (doc 08: one query, one answer) - e.g. unary_plus /
		// negate declared as [operator("unary_plus")] extends T. rhs is null so the catalog selects the
		// UNARY annotation. Primitives have no operator methods, so this is a no-op for them and the
		// built-in intrinsic below still applies.
		if (t && t->GetKind() == TypeKind::Object)
		{
			auto op = m_Ctx.queries().QueryOperator(tok, t, nullptr, m_Ctx.unitId, m_Ctx.enclosingType, AccessOf(operand.value));
			if (op.Ok() && op.value.record) return EmitOperatorCall(n, op.value, operand.value, Value());
			if (op.error == QueryError::ReadOnly) return m_Ctx.Fail(n, QueryResult<Value>::Fail(op.error, op.message));
		}

		if (Core::String why = BuiltinOperators::UnaryProblem(tok, t); !why.empty())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, why));

		if (const IntrinsicOp* ik = ModelQueries::IntrinsicForUnary(tok))
		{
			// Logical NOT yields a boolean (matching binary compares), lane by lane on a mask; numeric -/~ keep
			// the operand type.
			const bool scalarNot = *ik == IntrinsicOp::BoolNot && t->GetKind() != TypeKind::Swizzle;
			const Type* resT = scalarNot ? m_Ctx.queries().TypeStore()->Builtin(TypeKind::Bool) : t;
			const HIRNode* opNode = m_Ctx.LoadIfNeeded(operand.value);
			// C integer promotions: a narrow integral operand of unary - / ~ promotes to at least int32
			// BEFORE the op, so `-int16` and `~uint8` cannot wrap in their original width.
			if ((*ik == IntrinsicOp::Negate || *ik == IntrinsicOp::BitNot) && t->IsIntegral())
			{
				TypeKind ck = Type::UsualArithmeticResultKind(t->GetKind(), t->GetKind());
				if (ck != TypeKind::Invalid && ck != t->GetKind())
				{
					const Type* common = ts->Builtin(ck);
					opNode = m_Ctx.out->NewNode<ConvertNode>(opNode, opNode->type, common);
					resT = common; // the promoted result type (so `int32 r = -neg;` sees an int32)
				}
			}
			return Value::R(m_Ctx.out->NewNode<IntrinsicOpNode>(resT, *ik, opNode, nullptr), resT);
		}

		if (tok == "+") // unary plus is identity on primitives: +x == x (no op to emit)
			return Value::R(m_Ctx.LoadIfNeeded(operand.value), t);

		if (tok == "*") // dereference: pointer rvalue -> lvalue of the pointee
		{
			const Type* pointee = m_Ctx.queries().ElementOf(t);
			if (!pointee) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "cannot dereference a non-pointer"));
			LValue lv; lv.kind = LValue::Kind::Deref; lv.type = pointee; lv.addr = m_Ctx.LoadIfNeeded(operand.value);
			return Value::L(m_Ctx.NewLValue(lv));
		}
		if (tok == "&")
		{
			// `&Function` is the function's address, as the bare name is (C++).
			if (operand.value.IsRValue() && operand.value.rvalue->kind == HIRNode::Kind::FunctionAddress) return operand;
			if (!operand.value.isLValue) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "cannot take the address of an rvalue"));
			// D-37: lanes live in a SIMD register value, not at addresses of their own.
			const LValue::Kind kind = operand.value.lvalue->kind;
			if (kind == LValue::Kind::Lane || kind == LValue::Kind::VectorLaneDyn)
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue,
					"a lane of a packed vector has no address: copy it into a variable first, or take the address of the whole vector"));
			if (kind == LValue::Kind::Swizzle)
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue,
					"a swizzle run has no address: copy it into a 'packed' variable first"));
			const Type* ptrT = AddressType(m_Ctx, operand.value.lvalue); // `&x` of a read-only x is a `const T*`
			return Value::R(m_Ctx.out->NewNode<AddressOfNode>(ptrT, operand.value.lvalue), ptrT);
		}
		if (tok == "++" || tok == "--")
		{
			if (!operand.value.isLValue) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "'" + tok + "' requires an lvalue"));
			// An increment is a read-modify-write: through a by-value call temp (GetV().x++) or a const chain
			// the write would be discarded or mutate read-only data. Reject it rather than silently no-op.
			{
				Core::String why;
				if (ChainBlocksWrite(operand.value.lvalue, why))
					return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "'" + tok + "' cannot be applied: " + why));
			}
			// x is read and written: what it is computed from runs once (BUG-201).
			std::vector<const HIRNode*> steps;
			const LValue* x = EvaluatedOnce(m_Ctx, operand.value.lvalue, steps);
			const HIRNode* cur = m_Ctx.out->NewNode<LoadNode>(t, x); // x (pre-increment value)
			// The increment IS a store: x <- x +/- 1. For the PREFIX form this store is also the result,
			// because GcStore evaluates to its stored (NEW) value.
			const HIRNode* store = m_Ctx.out->NewNode<StoreNode>(x, Stepped(cur, tok == "++"));

			if (!n->IsPostfix())
				return Value::R(AfterSteps(m_Ctx, steps, store), t); // `++x` / `--x`: yield the NEW value

			// `x++` / `x--`: yield the OLD value while still performing the increment. The old value must be
			// captured BEFORE the store mutates x - a shared load of x would re-read after the write and return
			// the new value (the historical bug: post-increment behaved like pre, so `a = x++` gave the NEW
			// value and `arr[i++] = c` indexed one slot too far). Materialize it in a temp slot first, then
			// yield the temp. SequenceNode evaluates steps in order and yields the last.
			uint32 tmp = m_Ctx.AllocSlot(t, "__postinc_tmp", SlotClass::Local);
			LValue tmpLv; tmpLv.kind = LValue::Kind::Slot; tmpLv.type = t; tmpLv.slot = tmp;
			const HIRNode* loadOld = m_Ctx.out->NewNode<LoadNode>(t, x); // x (old)
			auto* seq = m_Ctx.out->NewNode<SequenceNode>(t);
			seq->steps = steps;
			seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(m_Ctx.NewLValue(tmpLv), loadOld)); // old -> tmp
			seq->steps.push_back(store);                                                            // x <- x +/- 1
			seq->steps.push_back(m_Ctx.out->NewNode<LoadNode>(t, m_Ctx.NewLValue(tmpLv)));          // yield tmp (old)
			return Value::R(seq, t);
		}
		if (tok == "delete") // deallocation: operand must be a pointer; codegen expands to [operator("delete")] + free
		{
			const Type* pointee = m_Ctx.queries().ElementOf(t);
			if (!pointee) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "cannot delete a non-pointer"));
			auto del = m_Ctx.out->NewNode<DeleteNode>(pointee, m_Ctx.LoadIfNeeded(operand.value));
			const Type* voidT = ts->Builtin(TypeKind::Void);
			return Value::R(del, voidT);
		}
		return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::Unsupported, "unary operator not yet lowered: " + tok));
	}

	std::optional<Value> ExprLowerer::SameLanesAs(const Value& operand, const Type* other)
	{
		const auto* run = TypeAs<SwizzleType>(other);
		const auto* object = TypeAs<ObjectType>(TypeHandle::UnwrapToValue(operand.type));
		if (!run || !run->IsPacked() || !object || !object->IsPurePackedVector()) return std::nullopt;
		const SwizzleType* lanes = object->GetPackedVectorSwizzle();
		if (lanes->GetLaneCount() != run->GetLaneCount() || lanes->GetElementType() != run->GetElementType()) return std::nullopt;
		return Value::R(m_Ctx.out->NewNode<ConvertNode>(m_Ctx.LoadIfNeeded(operand), object, run), run);
	}

	QueryResult<Value> ExprLowerer::VectorBinary(const AstNode* n, IntrinsicOp op, bool isCompare, const Value& lhs, const Value& rhs)
	{
		const Type* l = TypeHandle::UnwrapToValue(lhs.type);
		const Type* r = TypeHandle::UnwrapToValue(rhs.type);
		const auto* vector = static_cast<const SwizzleType*>(l->GetKind() == TypeKind::Swizzle ? l : r);
		const Type* other = l == vector ? r : l;
		if (other->GetKind() == TypeKind::Swizzle && other != vector)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "the operands are a '" + Core::String(l->GetName()) + "' and a '" +
				r->GetName() + "': two packed vectors combine only with the same element and lane count"));
		auto lanes = [&](const Value& v) {
			const HIRNode* node = m_Ctx.LoadIfNeeded(v);
			const Type* t = TypeHandle::UnwrapToValue(node->type);
			if (t->GetKind() == TypeKind::Swizzle || t == vector->GetElementType()) return node;
			return static_cast<const HIRNode*>(m_Ctx.out->NewNode<ConvertNode>(node, node->type, vector->GetElementType())); // broadcast after conversion
		};
		TypeStore* types = m_Ctx.queries().TypeStore();
		const Type* result = isCompare ? types->Swizzle(types->Builtin(TypeKind::Bool), vector->GetLaneCount()) : vector;
		return Value::R(m_Ctx.out->NewNode<IntrinsicOpNode>(result, op, lanes(lhs), lanes(rhs)), result);
	}

	const HIRNode* ExprLowerer::Stepped(const HIRNode* value, bool up)
	{
		// A pointer steps by one element (its pointee's size in bytes; a void* by one byte), a number by 1.
		const Type* t = value->type;
		if (t->GetKind() == TypeKind::Pointer)
		{
			const Type* pointee = TypeHandle::GetPointedType(t);
			const int64 size = pointee && pointee->GetSize() ? static_cast<int64>(pointee->GetSize()) : 1;
			const Type* i64 = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
			return m_Ctx.out->NewNode<AddressArithmeticNode>(t, value, m_Ctx.out->NewNode<ConstantNode>(i64, ConstantValue::Int(up ? size : -size)));
		}
		const ConstantValue one = t->IsFloatingPoint() ? ConstantValue::Float(1.0) : ConstantValue::Int(1);
		return m_Ctx.out->NewNode<IntrinsicOpNode>(t, up ? IntrinsicOp::Add : IntrinsicOp::Sub, value, m_Ctx.out->NewNode<ConstantNode>(t, one));
	}
} // namespace Scripting
