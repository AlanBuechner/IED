#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// One case in a switch: 'case' expr ':' statements*  OR  'default': statements*
	struct SwitchCaseNode : public AstNode
	{
		enum class CaseKind { Case, Default };

		SwitchCaseNode() = default;

		const Token& GetCaseKeywordToken() const { return m_CaseKeywordToken; }
		const Ref<AstNode>& GetConstantExpr() const { return m_ConstantExpr; }
		const Core::Vector<Ref<AstNode>>& GetStatements() const { return m_Statements; }
		CaseKind Kind() const { return m_Kind; }

		std::span<const Ref<AstNode>> Children() const override { return m_Statements; }
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::SwitchCase; }

	private:
		Token m_CaseKeywordToken;
		Ref<AstNode> m_ConstantExpr; // nullptr for default
		Token m_ColonToken;
		Core::Vector<Ref<AstNode>> m_Statements;
		CaseKind m_Kind = CaseKind::Case;

		friend struct SwitchStatementNode;
	};

	/// Switch statement: 'switch' '(' expr ')' '{' switch_section* '}'
	struct SwitchStatementNode : public AstNode
	{
		SwitchStatementNode() = default;

		const Ref<AstNode>& GetCondition() const { return m_Condition; }
		const Core::Vector<Ref<SwitchCaseNode>>& GetCases() const { return m_Cases; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override { return "switch"; }

		static Ref<SwitchStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::SwitchStatement; }

	private:
		Token m_SwitchToken;
		Token m_OpenParenToken;
		Ref<AstNode> m_Condition;
		Token m_CloseParenToken;
		Token m_OpenBraceToken;
		Core::Vector<Ref<SwitchCaseNode>> m_Cases;
		Token m_CloseBraceToken;
		Core::Vector<Ref<AstNode>> m_Children; // condition + cases
	};
}
