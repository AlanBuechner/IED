#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Core/Core.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Scripting
{
	/// A compile-time query (docs/language/10 4): `sizeof(T)` / `sizeof(expr)` / `sizeof expr`, `alignof(T)` /
	/// `alignof(expr)`, `offsetof(T, field.field)`, `__uuidof(T)` and `sizeof...(pack)`.
	///
	/// A parenthesized single name (`sizeof(x)`) parses as a type: whether it names a type or a value is the
	/// model's call, so the operand of that form is the type node and the lowering falls back to the value.
	struct TypeOfExpressionNode : public AstNode
	{
		enum class Kind { SizeOf, AlignOf, OffsetOf, UuidOf, SizeOfPack };

		TypeOfExpressionNode() = default;

		/// `sizeof`, `alignof`, `offsetof` or `__uuidof`.
		static bool IsKeyword(const Core::String& text);
		/// The query at the keyword `token` (already peeked, not consumed); null when it is not one.
		static Ref<TypeOfExpressionNode> TryParse(ParserState& state);

		Kind GetKind() const { return m_Kind; }
		const Token& GetKeywordToken() const { return m_KeywordToken; }
		/// The type operand, or null when the operand is an expression.
		const Ref<QualTypeNode>& GetTargetType() const { return m_TargetType; }
		/// The expression operand of `sizeof` / `alignof`, or null when the operand is a type.
		const Ref<AstNode>& GetOperand() const { return m_Operand; }
		/// `offsetof`'s field path (`a.b` is two names).
		const Core::Vector<Token>& GetFieldPath() const { return m_FieldPath; }
		const Token& GetPackName() const { return m_PackName; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::TypeOfExpression; }

	private:
		bool ParsePack(ParserState& state);
		bool ParseTypeOperand(ParserState& state);
		bool ParseFieldPath(ParserState& state);
		bool ParseExpressionOperand(ParserState& state);

		Ref<QualTypeNode> m_TargetType;
		Ref<AstNode> m_Operand;
		Core::Vector<Token> m_FieldPath;
		Token m_KeywordToken;
		Kind m_Kind = Kind::SizeOf;
		Token m_PackName;
		mutable Core::Vector<Ref<AstNode>> m_Children;
	};
}
