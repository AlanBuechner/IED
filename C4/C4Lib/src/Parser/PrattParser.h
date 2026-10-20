#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Binding powers of the expression grammar (D-27): C++'s levels, plus `**` above the prefix operators, whose
	/// operand binds at `**`'s power (Python's rule: `-x ** 2` is `-(x ** 2)`, `2 ** -1` works).
	namespace BindPower
	{
		constexpr uint32 Assignment = 5;      // `=`, compound assignments and `?:`, right associative
		constexpr uint32 LogicalOr = 10;
		constexpr uint32 LogicalAnd = 20;
		constexpr uint32 BitOr = 30;
		constexpr uint32 BitXor = 40;
		constexpr uint32 BitAnd = 50;
		constexpr uint32 Equality = 60;
		constexpr uint32 Relational = 70;     // `< <= > >=`, `is`, `as`
		constexpr uint32 Shift = 80;
		constexpr uint32 Additive = 90;
		constexpr uint32 Multiplicative = 100;
		constexpr uint32 Power = 120;         // `**`, right associative
		constexpr uint32 PrefixOperand = Power;
	}

	struct TemplateArgumentListNode;
	struct QualTypeNode;

	/// Top-Down Operator Precedence parser (Pratt Parser).
	/// Powers all expression parsing with proper operator precedence and associativity.
	struct PrattParser
	{
		/// Main entry point - parses an expression up to the given bind power.
		static Ref<AstNode> ParseExpression(ParserState& state, uint32 minBindPower = 0);
		/// An initializer entry's key (`x` in `{ x = 1 }`): an expression that stops before an assignment.
		static Ref<AstNode> ParseDesignator(ParserState& state);

		/// An expression inside `< >` (a template argument or default): `<`, `>` and every looser operator
		/// end it, so `Arr<N + 1>` works and a comparison needs parentheses, as in C++.
		static Ref<AstNode> ParseTemplateArgumentExpression(ParserState& state);

		/// A call argument or braced-list element: an expression, or a pack expansion `pattern...` (D-19).
		static Ref<AstNode> ParseArgument(ParserState& state);
		/// `parsed...` when a `...` follows it, else `parsed` (template arguments, D-19).
		static Ref<AstNode> ExpandIfPack(ParserState& state, const Ref<AstNode>& parsed);

	private:
		/// Parse a prefix (primary) expression: literals, identifiers, unary operators, parenthesized expressions, casts.
		static Ref<AstNode> ParsePrefix(ParserState& state);

		/// Parse an infix operator and its right operand.
		static Ref<AstNode> ParseInfix(ParserState& state, Ref<AstNode> left, uint32 leftBindPower);

		/// The type after `is` / `as`, or null with an error naming the keyword.
		static Ref<QualTypeNode> ParseTargetType(ParserState& state, const Token& keyword);

		/// Parse postfix operators (chained): (), [], ., ->, ::, ++, --.
		static Ref<AstNode> ParsePostfix(ParserState& state, Ref<AstNode> base);

		/// Parse a parenthesized expression or C-style cast disambiguation.
		static Ref<AstNode> ParseParenOrCStyleCast(ParserState& state);
		/// Inside `( ... )`: a fold expression when a `...` follows an operator at this nesting level, else null.
		static Ref<AstNode> ParseFold(ParserState& state, const Token& openParen);
		static Ref<AstNode> TryMemberTemplateCall(ParserState& state, const Ref<AstNode>& member);
		static TokenType FoldOperator(Lexer& lexer);
		static bool IsFoldOperator(TokenType type);

		/// After a complete `name<args>`: a token that cannot start the right operand of the `>` in a comparison
		/// `name < a > b`, so `name<args>` is a template's instance used as a value (`funcptr<...> f = Max<int32>;`), as
		/// in C#'s rule (without `&`, which starts an address).
		static bool EndsTemplateName(TokenType type);

		/// `callee<args>(...)` once the template arguments are parsed and '(' is next: the call node.
		static Ref<AstNode> FinishTemplateCall(ParserState& state, Ref<AstNode> callee, Ref<TemplateArgumentListNode> tmplArgs,
			const SourceLocation& start);

		/// Parse a C++-style cast: static_cast<T>(expr), etc.
		static Ref<AstNode> ParseCppCast(ParserState& state, Token keywordToken);

		/// Bind power for infix operators. Returns 0 if not an infix operator.
		static uint32 InfixBindPower(TokenType tokenType);

		/// Whether the infix operator is right-associative.
		static bool IsRightAssociative(TokenType tokenType);
	};
}
