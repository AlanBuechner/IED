#pragma once
#include "Parser/AstNode.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// `pattern...` (D-19): the pattern once per element of the packs it names, in the arguments of a call, the
	/// elements of a braced list, or a template argument list - `G(args...)`, `G(Wrap(args)...)`, `Tuple<Ts...>`.
	struct PackExpansionNode : public AstNode
	{
		static Ref<PackExpansionNode> Around(const Ref<AstNode>& pattern, const Token& ellipsis);

		const Ref<AstNode>& GetPattern() const { return m_Children[0]; }
		const Token& GetEllipsisToken() const { return m_Ellipsis; }

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override { return GetPattern()->ToString() + "..."; }

	protected:
		NodeType Type() const override { return NodeType::PackExpansion; }

	private:
		Core::Vector<Ref<AstNode>> m_Children; // the pattern
		Token m_Ellipsis;
	};

	/// A fold over a pack (D-19): `(args + ...)` and `(args + ... + init)` fold from the right, `(... + args)` and
	/// `(init + ... + args)` from the left. Which operand is the pack is decided where the packs are known.
	struct FoldExpressionNode : public AstNode
	{
		static Ref<FoldExpressionNode> Of(const Token& op, const Ref<AstNode>& first, const Ref<AstNode>& last, const SourceLocation& start,
			const SourceLocation& end);

		const Token& GetOperatorToken() const { return m_Operator; }
		/// The operand written before `...`; null in `(... + args)`.
		const Ref<AstNode>& GetFirst() const { return m_First; }
		/// The operand written after `...`; null in `(args + ...)`.
		const Ref<AstNode>& GetLast() const { return m_Last; }

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::FoldExpression; }

	private:
		Token m_Operator;
		Ref<AstNode> m_First;
		Ref<AstNode> m_Last;
		Core::Vector<Ref<AstNode>> m_Children;
	};
} // namespace Scripting
