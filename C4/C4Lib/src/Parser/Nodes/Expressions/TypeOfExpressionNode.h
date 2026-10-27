#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Core/Core.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Scripting
{
	/// A compile-time query (docs/language/10 4): `sizeof(T)` / `sizeof(expr)` / `sizeof expr`, `alignof(T)` /
	/// `alignof(expr)`, `offsetof(T, field.field)`, `holds_native_bytes(T)`, `holds_pointers(T)`, `__uuidof(T)`, `nameof(T)` /
	/// `nameof(expr)` and `sizeof...(pack)`.
	///
	/// A parenthesized single name (`sizeof(x)`) parses as a type: whether it names a type or a value is the
	/// model's call, so the operand of that form is the type node and the lowering falls back to the value. The two
	/// `holds_*` queries also keep their text as a call: a program function of that name wins over the query.
	struct TypeOfExpressionNode : public AstNode
	{
		enum class Kind { SizeOf, AlignOf, OffsetOf, UuidOf, NameOf, SizeOfPack, HoldsNativeBytes, HoldsPointers };

		TypeOfExpressionNode() = default;

		/// `sizeof`, `alignof`, `offsetof`, `holds_native_bytes`, `holds_pointers`, `__uuidof` or `nameof`.
		static bool IsKeyword(const Core::String& text);
		/// The query at the keyword `token` (already peeked, not consumed); null when it is not one, or when a
		/// `holds_*` keyword is followed by arguments that are no type (an ordinary call then parses them).
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
		/// The same text as a call, `holds_pointers(T)`, for a program function of that name; null for the other kinds.
		const Ref<AstNode>& GetCall() const { return m_Call; }

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::TypeOfExpression; }

	private:
		bool ParsePack(ParserState& state);
		bool ParseTypeOperand(ParserState& state);
		bool ParseFieldPath(ParserState& state);
		bool ParseExpressionOperand(ParserState& state);
		/// The operand, a type or an expression, and Children with it.
		void SetOperand(Ref<QualTypeNode> type, Ref<AstNode> expression);

		Ref<QualTypeNode> m_TargetType;
		Ref<AstNode> m_Operand;
		Ref<AstNode> m_Call;
		Core::Vector<Token> m_FieldPath;
		Token m_KeywordToken;
		Kind m_Kind = Kind::SizeOf;
		Token m_PackName;
		Core::Vector<Ref<AstNode>> m_Children; // the type operand, else the expression
	};
}
