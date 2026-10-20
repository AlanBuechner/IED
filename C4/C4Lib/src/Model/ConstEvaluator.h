#pragma once
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"
#include "Core/Core.h"

namespace Scripting
{
	struct LiteralNode;
	struct TypeOfExpressionNode;
	class Type;
	struct SymbolInfo;
	class ProgramModel; // forward - the evaluator asks it for constants and types (no cycle: model includes this)
	struct AstNode;     // expression nodes are parsed by the front end; only pointers flow here
	struct Scope;       // a template instantiation's binding layer, when folding inside one
	struct FoldExpressionNode;
	struct CastExpressionNode;

	/// Folds compile-time constant expressions to a value (doc 02 pass 3). Handles literals,
	/// arithmetic/bitwise/logical operators on ints and floats, unary +/-/!, identifier references
	/// to other constants (resolved through the model - so a const may name another const or an
	/// enum member), sizeof, and parenthesized subexpressions. Order-independent: folds to a
	/// fixpoint with cycle detection (a const naming itself is CyclicDependency, not a hang).
	class ConstEvaluator
	{
	public:
		/// `context` is the scope the expressions are written in (a namespace, a class, a block): a constant's
		/// name resolves from there outward, as an identifier in code does. Null resolves from the root.
		explicit ConstEvaluator(const ProgramModel& model, const Scope* context = nullptr) : m_Model(model), m_Context(context) {}

		/// Fold an expression AST node. `bindingScope` (optional) is a template instantiation's
		/// binding layer: identifiers resolve there FIRST, so a non-type parameter like `N` in
		/// `int32[N]` folds to its bound value. Returns NotAValue when the expression is not
		/// constant (e.g. it names a mutable variable), CyclicDependency on circular const refs.
		QueryResult<ConstantValue> Evaluate(const AstNode* expr, uint32 unitId = 0, Scope* bindingScope = nullptr) const;

		/// The value a constant's name (`N`, `Limits::Count`) folds to, by the same rule as an identifier in
		/// an expression: a template's bound value first, then a local constant, then the program's.
		QueryResult<ConstantValue> EvaluateName(const Core::String& name, uint32 unitId = 0, Scope* bindingScope = nullptr) const;

		/// One program-wide folding sweep (doc 02 pass-2 fixed point): every const variable whose
		/// initializer is now constant gets its value stored on the symbol. True when anything folded
		/// this sweep - the resolver iterates until a full sweep changes nothing. Order-independent
		/// because a const may name another const or an enum member that folds in the same round.
		static bool FoldGlobalConstants(const ProgramModel& model);

		/// The value a variable of `type` starts holding from the constant `initializer` (StoredConstant): its
		/// folded value converted to the type, or for a pointer `null` or a string literal.
		QueryResult<ConstantValue> EvaluateStart(const AstNode* initializer, const Type* type, uint32 unitId = 0, Scope* bindingScope = nullptr) const;

		/// A declared constant's value, folded from its initializer where it is declared. A constant that
		/// depends on itself is CyclicDependency, with the loop in the message.
		QueryResult<ConstantValue> EvaluateConstant(const SymbolInfo& symbol) const;

	private:
		using Visiting = Core::Vector<const SymbolInfo*>; // the constants whose initializers are folding
		QueryResult<ConstantValue> EvaluateInner(const AstNode* expr, uint32 unitId, Visiting& visiting, Scope* bindingScope) const;
		QueryResult<ConstantValue> FoldBinary(const Core::String& op, const AstNode* left, const AstNode* right, uint32 unitId, Visiting& visiting, Scope* bindingScope) const;
		QueryResult<ConstantValue> PackLength(const Core::String& name, Scope* bindingScope) const;
		QueryResult<ConstantValue> FoldPack(const FoldExpressionNode& fold, uint32 unitId, Visiting& visiting, Scope* bindingScope) const;
		/// `a op b` for two folded constants.
		static QueryResult<ConstantValue> Combine(const Core::String& op, const ConstantValue& a, const ConstantValue& b);
		static QueryResult<ConstantValue> CombineIntegers(const Core::String& op, int64 a, int64 b);
		static QueryResult<ConstantValue> CombineFloats(const Core::String& op, double a, double b);
		static constexpr int64 kShiftLimit = 64; // a shift by this many bits or more has no value
		static QueryResult<ConstantValue> FoldLiteral(const LiteralNode& literal);
		static QueryResult<ConstantValue> AddressLiteral(const AstNode* initializer);
		QueryResult<ConstantValue> FoldCast(const CastExpressionNode& cast, uint32 unitId, Visiting& visiting, Scope* bindingScope) const;
		QueryResult<ConstantValue> FoldQuery(const TypeOfExpressionNode& query, uint32 unitId, Scope* bindingScope) const;
		QueryResult<const Type*> QueryOperandType(const TypeOfExpressionNode& query, uint32 unitId, Scope* bindingScope) const;
		QueryResult<const Type*> ExpressionType(const AstNode* expr, uint32 unitId) const;
		QueryResult<const Type*> LiteralType(const LiteralNode& literal) const;
		QueryResult<ConstantValue> EvaluateNamed(const Core::String& name, uint32 unitId, Visiting& visiting, Scope* bindingScope) const;
		QueryResult<ConstantValue> FoldSymbol(const Core::String& name, const SymbolInfo& symbol, uint32 unitId, Visiting& visiting) const;
		static Core::String LoopMessage(const Visiting& visiting, const SymbolInfo& symbol);
		static bool IsIntKind(ConstantValue::Kind k) { return k == ConstantValue::Kind::Int64 || k == ConstantValue::Kind::Bool; }

		const ProgramModel& m_Model;
		const Scope* m_Context;
	};

} // namespace Scripting
