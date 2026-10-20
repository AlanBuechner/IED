#include "Model/ConstEvaluator.h"
#include <algorithm>
#include <cmath>
#include "Model/ConstConversions.h"
#include "Model/Literals.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ObjectType.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include "Parser/Nodes/Expressions/UnaryExpressionNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"
#include "Model/Templates/Packs.h"
#include "Parser/Nodes/Expressions/ConditionalAndNode.h"
#include "Parser/Nodes/Expressions/ConditionalOrNode.h"
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/TernaryExpressionNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include <vector>

namespace Scripting
{
	// Build the qualified name path for a constant expression, or "" when it is not a (qualified)
	// name. A plain identifier returns "" so the caller's identifier branch handles it (that branch
	// also checks template binding scopes). A StaticAccessNode chain (`E::X`, `Ns::C::K`) and a
	// NamespaceQualifiedNameNode both collapse to "a::b::c" for QueryConstant.
	static Core::String QualifiedConstantPath(const AstNode* expr)
	{
		if (!expr) return "";
		if (dynamic_cast<const IdentifierExpressionNode*>(expr)) return ""; // handled by the identifier branch
		if (const auto* sa = dynamic_cast<const StaticAccessNode*>(expr))
		{
			const Core::String base = QualifiedConstantPath(sa->GetBaseType().Get());
			const Core::String member = sa->GetMemberNameToken().GetText();
			// A plain-identifier base (the common `E::X` form) must be INCLUDED in the path: scoped
			// enum members are not injected into any scope, so QueryConstant("X") alone cannot find
			// them - only the qualified "E::X" resolves. (Unscoped enums work either way.)
			if (base.empty())
			{
				if (const auto* ident = dynamic_cast<const IdentifierExpressionNode*>(sa->GetBaseType().Get()))
					return ident->GetName() + "::" + member;
				return member; // no resolvable base - leave it for the identifier branch to report
			}
			return base + "::" + member;
		}
		if (const auto* qual = dynamic_cast<const NamespaceQualifiedNameNode*>(expr))
		{
			Core::String path;
			for (uint32 s = 0; s < qual->SegmentCount(); ++s)
			{
				if (!path.empty()) path += "::";
				path += qual->GetSegments()[s].nameToken.GetText();
			}
			return path;
		}
		return "";
	}

	QueryResult<ConstantValue> ConstEvaluator::EvaluateName(const Core::String& name, uint32 unitId, Scope* bindingScope) const
	{
		Visiting visiting;
		return EvaluateNamed(name, unitId, visiting, bindingScope);
	}

	QueryResult<ConstantValue> ConstEvaluator::EvaluateNamed(const Core::String& name, uint32 unitId, Visiting& visiting, Scope* bindingScope) const
	{
		// A template's value parameter is a bound const in the binding layer, a function template's (or any
		// local const) is in the lowering chain; both shadow a program constant of the same name. Only a
		// bound value counts; anything else is the model's (folded consts and enum members).
		auto boundValue = [](const SymbolInfo* s) {
			return s && s->kind == SymbolKind::Variable && s->isConst && !s->constValue.IsNone();
		};
		if (name.find("::") == Core::String::npos && Packs::Find(m_Model, name, bindingScope))
			return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "the pack '" + name + "' must be expanded with '...'");
		if (bindingScope && name.find("::") == Core::String::npos)
			if (const SymbolInfo* bound = bindingScope->Find(name); boundValue(bound)) return QueryResult<ConstantValue>::Success(bound->constValue);
		// A local shadows every program constant: it folds only when it is a const bound to a value.
		if (const SymbolInfo* local = m_Model.FindLocal(name); local && local->kind == SymbolKind::Variable)
		{
			if (boundValue(local)) return QueryResult<ConstantValue>::Success(local->constValue);
			return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, local->isConst ? "'" + name + "' is a const whose initializer runs at run time"
				: "'" + name + "' is a variable: only a const with a constant initializer folds");
		}
		auto symbol = m_Model.QuerySymbol(name, unitId, m_Context);
		if (!symbol.Ok()) return m_Model.QueryConstant(name, unitId, m_Context); // a scoped enum's member is not a symbol
		return FoldSymbol(name, *symbol.value, unitId, visiting);
	}

	// Not folded yet: fold the initializer where the constant is declared, so a chain folds in any order. A
	// constant reached again while its own initializer is folding depends on itself.
	QueryResult<ConstantValue> ConstEvaluator::FoldSymbol(const Core::String& name, const SymbolInfo& symbol, uint32 unitId, Visiting& visiting) const
	{
		if (const Type* type = TypeHandle::UnwrapToValue(symbol.type); type && type->IsNormalized())
			return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'" + name + "' is a normalized value, a fraction the constant does not fold over");
		const bool unfolded = symbol.kind == SymbolKind::Variable && symbol.isConst && symbol.constValue.IsNone() && symbol.initializer;
		if (!unfolded) return m_Model.QueryConstant(name, unitId, m_Context);
		if (std::find(visiting.begin(), visiting.end(), &symbol) != visiting.end())
			return QueryResult<ConstantValue>::Fail(QueryError::CyclicDependency, LoopMessage(visiting, symbol));
		visiting.Push(&symbol);
		auto r = ConstEvaluator(m_Model, symbol.declaringScope).EvaluateInner(symbol.initializer, symbol.definingUnit, visiting, nullptr);
		visiting.Remove(visiting.Count() - 1);
		if (r.Ok()) r = StoredConstant(r.value, symbol.type);
		if (!r.Ok() && r.error == QueryError::NotAValue) r.message = "'" + name + "' starts at runtime: " + r.message;
		return r;
	}

	Core::String ConstEvaluator::LoopMessage(const Visiting& visiting, const SymbolInfo& symbol)
	{
		Core::String loop;
		bool inLoop = false;
		for (const SymbolInfo* step : visiting)
		{
			inLoop = inLoop || step == &symbol;
			if (inLoop) loop += step->plainName + " -> ";
		}
		return "the constant '" + symbol.plainName + "' depends on itself: " + loop + symbol.plainName;
	}

	QueryResult<ConstantValue> ConstEvaluator::EvaluateConstant(const SymbolInfo& symbol) const
	{
		Visiting visiting;
		return FoldSymbol(symbol.plainName, symbol, symbol.definingUnit, visiting);
	}

	QueryResult<ConstantValue> ConstEvaluator::Evaluate(const AstNode* expr, uint32 unitId, Scope* bindingScope) const
	{
		Visiting visiting; // cycle detection for const -> const chains
		return EvaluateInner(expr, unitId, visiting, bindingScope);
	}

	QueryResult<ConstantValue> ConstEvaluator::FoldLiteral(const LiteralNode& literal)
	{
		switch (literal.GetKind())
		{
		case LiteralNode::LiteralKind::Boolean:
			return QueryResult<ConstantValue>::Success(ConstantValue::Bool(literal.GetValue() == "true"));
		case LiteralNode::LiteralKind::Char:
			return QueryResult<ConstantValue>::Success(ConstantValue::Int(CharacterValue(literal.GetValue(), literal.GetUnitWidth())));
		case LiteralNode::LiteralKind::Number:
		{
			auto number = ParseNumericLiteral(literal.GetValue());
			if (!number.Ok()) return QueryResult<ConstantValue>::Fail(number.error, number.message);
			return QueryResult<ConstantValue>::Success(number.value.IsFloat() ? ConstantValue::Float(number.value.real)
				: ConstantValue::Int(static_cast<int64>(number.value.bits)));
		}
		default:
			return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "non-constant literal");
		}
	}

	// `null` and a string literal are constant only as a pointer's start, never as an operand.
	QueryResult<ConstantValue> ConstEvaluator::AddressLiteral(const AstNode* initializer)
	{
		const auto* literal = dynamic_cast<const LiteralNode*>(initializer);
		if (literal && literal->GetKind() == LiteralNode::LiteralKind::Null) return QueryResult<ConstantValue>::Success(ConstantValue::Int(0));
		if (literal && literal->GetKind() == LiteralNode::LiteralKind::String)
			return QueryResult<ConstantValue>::Success(ConstantValue::String(literal->GetValue(), literal->GetUnitWidth()));
		return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "not an address literal");
	}

	QueryResult<ConstantValue> ConstEvaluator::EvaluateStart(const AstNode* initializer, const Type* type, uint32 unitId, Scope* bindingScope) const
	{
		auto value = AddressLiteral(initializer);
		if (!value.Ok()) value = Evaluate(initializer, unitId, bindingScope);
		if (!value.Ok()) return value;
		return StoredConstant(value.value, type);
	}

	QueryResult<ConstantValue> ConstEvaluator::EvaluateInner(const AstNode* expr, uint32 unitId, Visiting& visiting, Scope* bindingScope) const
	{
		if (!expr) return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "null expression");

		// Literal: number, bool, char.
		if (const auto* lit = dynamic_cast<const LiteralNode*>(expr))
			return FoldLiteral(*lit);

		// Parenthesized: fold the inner.
		if (const auto* paren = dynamic_cast<const ParenthesizedExpressionNode*>(expr))
			return EvaluateInner(paren->GetExpression().Get(), unitId, visiting, bindingScope);

		// `c ? a : b`: the branch the folded condition picks (the other is never evaluated, as at run time).
		if (const auto* choice = dynamic_cast<const TernaryExpressionNode*>(expr))
		{
			auto condition = EvaluateInner(choice->GetCondition().Get(), unitId, visiting, bindingScope);
			if (!condition.Ok()) return condition;
			const bool taken = condition.value.kind == ConstantValue::Kind::Float64 ? condition.value.floatValue != 0.0 : condition.value.AsInt() != 0;
			return EvaluateInner((taken ? choice->GetThenBranch() : choice->GetElseBranch()).Get(), unitId, visiting, bindingScope);
		}

		// Unary: -, +, !, ~ (a negation wraps, as it does at run time).
		if (const auto* unary = dynamic_cast<const UnaryExpressionNode*>(expr))
		{
			auto inner = EvaluateInner(unary->GetOperand().Get(), unitId, visiting, bindingScope);
			if (!inner.Ok()) return inner;
			const Core::String& op = unary->GetOperatorToken().GetText();
			const bool integral = IsIntKind(inner.value.kind);
			if (op == "-") {
				if (integral) return QueryResult<ConstantValue>::Success(ConstantValue::Int(static_cast<int64>(0 - static_cast<uint64>(inner.value.AsInt()))));
				return QueryResult<ConstantValue>::Success(ConstantValue::Float(-inner.value.floatValue));
			}
			if (op == "+") return inner;
			if (op == "!") return QueryResult<ConstantValue>::Success(ConstantValue::Bool(integral ? inner.value.AsInt() == 0 : inner.value.floatValue == 0.0));
			if (op == "~" && integral) return QueryResult<ConstantValue>::Success(ConstantValue::Int(~inner.value.AsInt()));
			if (op == "~") return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'~' needs an integer operand in a constant");
			return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "unsupported unary op in const expr: " + op);
		}

		if (const auto* cast = dynamic_cast<const CastExpressionNode*>(expr)) return FoldCast(*cast, unitId, visiting, bindingScope);

		// Binary arithmetic / comparison, and the logical operators (their own node kinds).
		if (const auto* bin = dynamic_cast<const BinaryExpressionNode*>(expr))
			return FoldBinary(bin->GetOperatorToken().GetText(), bin->GetLeftOperand().Get(), bin->GetRightOperand().Get(), unitId, visiting, bindingScope);
		if (const auto* conjunction = dynamic_cast<const ConditionalAndNode*>(expr))
			return FoldBinary("&&", conjunction->GetLeftOperand().Get(), conjunction->GetRightOperand().Get(), unitId, visiting, bindingScope);
		if (const auto* disjunction = dynamic_cast<const ConditionalOrNode*>(expr))
			return FoldBinary("||", disjunction->GetLeftOperand().Get(), disjunction->GetRightOperand().Get(), unitId, visiting, bindingScope);
		if (const auto* fold = dynamic_cast<const FoldExpressionNode*>(expr)) return FoldPack(*fold, unitId, visiting, bindingScope);

	// Identifier: resolve through the model as a constant (with cycle detection).
	if (const auto* ident = dynamic_cast<const IdentifierExpressionNode*>(expr))
		return EvaluateNamed(ident->GetName(), unitId, visiting, bindingScope);

		// Qualified constant: a qualified enum member (`E::X`) or namespaced const used as a value
		// (e.g. an array size `int32[E::X] g;`). In expression position the parser yields a
		// StaticAccessNode chain (base :: member) or a plain identifier; fold it through the model's
		// constant query, which walks namespaces and scoped/unscoped enum members for qualified names.
		if (const Core::String path = QualifiedConstantPath(expr); !path.empty())
			return EvaluateNamed(path, unitId, visiting, bindingScope);

		if (const auto* query = dynamic_cast<const TypeOfExpressionNode*>(expr))
			return FoldQuery(*query, unitId, bindingScope);

		return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'" + expr->ToString() + "' is not one of the forms a constant folds from "
			"(literals, constants, enum members, sizeof and alignof, and operators on them)");
	}

	QueryResult<ConstantValue> ConstEvaluator::PackLength(const Core::String& name, Scope* bindingScope) const
	{
		const SymbolInfo* pack = Packs::Find(m_Model, name, bindingScope);
		if (!pack) return QueryResult<ConstantValue>::Fail(QueryError::NotFound, "sizeof...(" + name + "): '" + name + "' is not a pack");
		return QueryResult<ConstantValue>::Success(ConstantValue::Int(static_cast<int64>(pack->packElements->size())));
	}

	QueryResult<ConstantValue> ConstEvaluator::FoldBinary(const Core::String& op, const AstNode* left, const AstNode* right, uint32 unitId, Visiting& visiting, Scope* bindingScope) const
	{
		auto l = EvaluateInner(left, unitId, visiting, bindingScope);
		if (!l.Ok()) return l;
		auto r = EvaluateInner(right, unitId, visiting, bindingScope);
		if (!r.Ok()) return r;
		return Combine(op, l.value, r.value);
	}

	QueryResult<ConstantValue> ConstEvaluator::FoldPack(const FoldExpressionNode& fold, uint32 unitId, Visiting& visiting,
		Scope* bindingScope) const
	{
		// The fold's copies, folded as at run time (docs/language/08 6): from the right `e0 op (e1 op init)`, from
		// the left `(init op e0) op e1`; an empty `&&` is true and an empty `||` false.
		using Result = QueryResult<ConstantValue>;
		const Core::String op = fold.GetOperatorToken().GetText();
		const AstNode* first = fold.GetFirst().Get();
		const AstNode* last = fold.GetLast().Get();
		const bool fromRight = !last || (first && Packs::Names(m_Model, first, bindingScope));
		const AstNode* pattern = fromRight ? first : last;
		const AstNode* initial = fromRight ? last : first;
		auto packs = Packs::In(m_Model, pattern, bindingScope);
		if (!packs.Ok()) return Result::Fail(packs.error, packs.message);
		std::vector<ConstantValue> values;
		for (uint32 i = 0; i < Packs::Length(packs.value); ++i)
		{
			auto v = EvaluateInner(pattern, unitId, visiting, Packs::ElementScope(m_Model, packs.value, i, bindingScope));
			if (!v.Ok()) return v;
			values.push_back(v.value);
		}
		if (initial)
		{
			auto v = EvaluateInner(initial, unitId, visiting, bindingScope);
			if (!v.Ok()) return v;
			values.insert(fromRight ? values.end() : values.begin(), v.value);
		}
		if (values.empty())
		{
			if (op == "&&" || op == "||") return Result::Success(ConstantValue::Bool(op == "&&"));
			return Result::Fail(QueryError::NotAValue, "the fold '" + fold.ToString() + "' is over an empty pack");
		}
		if (op == ",") return Result::Success(values.back());
		ConstantValue folded = fromRight ? values.back() : values.front();
		for (size_t step = 1; step < values.size(); ++step)
		{
			auto r = fromRight ? Combine(op, values[values.size() - 1 - step], folded) : Combine(op, folded, values[step]);
			if (!r.Ok()) return r;
			folded = r.value;
		}
		return Result::Success(folded);
	}

	// The folded `base ** exponent` on integers: the same wrapping square-and-multiply the program runs (D-26).
	static int64 IntegerPower(int64 base, int64 exponent)
	{
		if (exponent < 0) return base == 1 ? 1 : base == -1 ? ((exponent & 1) ? -1 : 1) : 0;
		uint64 result = 1, square = static_cast<uint64>(base);
		for (uint64 rest = static_cast<uint64>(exponent); rest != 0; rest >>= 1)
		{
			if (rest & 1) result *= square;
			square *= square;
		}
		return static_cast<int64>(result);
	}

	// Integer arithmetic is the run time's: + - * wrap (two's complement), / and % truncate toward zero; what has no
	// value there (a division by zero, a shift by a negative count or by 64 or more) is not a constant.
	QueryResult<ConstantValue> ConstEvaluator::Combine(const Core::String& op, const ConstantValue& a, const ConstantValue& b)
	{
		using Result = QueryResult<ConstantValue>;
		if (IsIntKind(a.kind) && IsIntKind(b.kind)) return CombineIntegers(op, a.AsInt(), b.AsInt());
		const bool floating = a.kind == ConstantValue::Kind::Float64 || b.kind == ConstantValue::Kind::Float64;
		if (floating && (IsIntKind(a.kind) || a.kind == ConstantValue::Kind::Float64) && (IsIntKind(b.kind) || b.kind == ConstantValue::Kind::Float64))
		{
			const double av = a.kind == ConstantValue::Kind::Float64 ? a.floatValue : static_cast<double>(a.AsInt());
			const double bv = b.kind == ConstantValue::Kind::Float64 ? b.floatValue : static_cast<double>(b.AsInt());
			return CombineFloats(op, av, bv);
		}
		return Result::Fail(QueryError::NotAValue, "unsupported binary op in const expr: " + op);
	}

	QueryResult<ConstantValue> ConstEvaluator::CombineIntegers(const Core::String& op, int64 av, int64 bv)
	{
		using Result = QueryResult<ConstantValue>;
		const uint64 ua = static_cast<uint64>(av), ub = static_cast<uint64>(bv);
		if (op == "+") return Result::Success(ConstantValue::Int(static_cast<int64>(ua + ub)));
		if (op == "-") return Result::Success(ConstantValue::Int(static_cast<int64>(ua - ub)));
		if (op == "*") return Result::Success(ConstantValue::Int(static_cast<int64>(ua * ub)));
		if (op == "/" || op == "%")
		{
			if (bv == 0) return Result::Fail(QueryError::NotAValue, "division by zero in const expr");
			if (bv == -1) return Result::Success(ConstantValue::Int(op == "/" ? static_cast<int64>(0 - ua) : 0)); // INT64_MIN / -1 wraps
			return Result::Success(ConstantValue::Int(op == "/" ? av / bv : av % bv));
		}
		if (op == "<<" || op == ">>")
		{
			if (bv < 0 || bv >= kShiftLimit)
				return Result::Fail(QueryError::NotAValue, "the shift count " + std::to_string(bv) + " is outside 0 to 63 in const expr");
			return Result::Success(ConstantValue::Int(op == "<<" ? static_cast<int64>(ua << bv) : av >> bv));
		}
		if (op == "**") return Result::Success(ConstantValue::Int(IntegerPower(av, bv)));
		if (op == "&") return Result::Success(ConstantValue::Int(av & bv));
		if (op == "|") return Result::Success(ConstantValue::Int(av | bv));
		if (op == "^") return Result::Success(ConstantValue::Int(av ^ bv));
		if (op == "==") return Result::Success(ConstantValue::Bool(av == bv));
		if (op == "!=") return Result::Success(ConstantValue::Bool(av != bv));
		if (op == "<") return Result::Success(ConstantValue::Bool(av < bv));
		if (op == ">") return Result::Success(ConstantValue::Bool(av > bv));
		if (op == "<=") return Result::Success(ConstantValue::Bool(av <= bv));
		if (op == ">=") return Result::Success(ConstantValue::Bool(av >= bv));
		if (op == "&&") return Result::Success(ConstantValue::Bool(av != 0 && bv != 0));
		if (op == "||") return Result::Success(ConstantValue::Bool(av != 0 || bv != 0));
		return Result::Fail(QueryError::NotAValue, "unsupported binary op in const expr: " + op);
	}

	QueryResult<ConstantValue> ConstEvaluator::CombineFloats(const Core::String& op, double av, double bv)
	{
		using Result = QueryResult<ConstantValue>;
		if (op == "+") return Result::Success(ConstantValue::Float(av + bv));
		if (op == "-") return Result::Success(ConstantValue::Float(av - bv));
		if (op == "*") return Result::Success(ConstantValue::Float(av * bv));
		if (op == "/" || op == "%")
		{
			if (bv == 0.0) return Result::Fail(QueryError::NotAValue, "division by zero in const expr");
			return Result::Success(ConstantValue::Float(op == "/" ? av / bv : std::fmod(av, bv)));
		}
		if (op == "**") return Result::Success(ConstantValue::Float(std::pow(av, bv)));
		if (op == "==") return Result::Success(ConstantValue::Bool(av == bv));
		if (op == "!=") return Result::Success(ConstantValue::Bool(av != bv));
		if (op == "<") return Result::Success(ConstantValue::Bool(av < bv));
		if (op == ">") return Result::Success(ConstantValue::Bool(av > bv));
		if (op == "<=") return Result::Success(ConstantValue::Bool(av <= bv));
		if (op == ">=") return Result::Success(ConstantValue::Bool(av >= bv));
		if (op == "&&") return Result::Success(ConstantValue::Bool(av != 0.0 && bv != 0.0));
		if (op == "||") return Result::Success(ConstantValue::Bool(av != 0.0 || bv != 0.0));
		return Result::Fail(QueryError::NotAValue, "'" + op + "' needs integer operands, not floating-point, in a constant");
	}


	// --- Program-wide constant folding (doc 02 pass-2 fixed point) ----------------------------
	bool ConstEvaluator::FoldGlobalConstants(const ProgramModel& model)
	{
		bool changed = false;
		model.Symbols()->ForEachScopedSymbol([&](const Core::String&, SymbolInfo* info, const Scope& scope)
		{
			// Fold EVERY global's constant initializer (const AND non-const): codegen uses the folded value as the
			// GlobalVariable's starting value (doc 03: "the model's constant values) or zero-init"). A non-constant
			// initializer fails to fold and stays None, so that global correctly falls back to zero-init.
			if (info->kind != SymbolKind::Variable || !info->initializer || !info->constValue.IsNone())
				return;
			ConstEvaluator eval(model, &scope);
			auto v = eval.EvaluateStart(info->initializer, info->type);
			if (v.Ok() && !v.value.IsNone())
			{
				info->constValue = v.value; // mutable until the model is sealed
				changed = true;
			}
		});
		return changed;
	}

} // namespace Scripting

