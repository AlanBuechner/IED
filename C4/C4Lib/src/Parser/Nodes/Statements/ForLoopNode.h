#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	struct CodeBlockNode;

	/// For loop supporting C-style and range-based forms.
	/// Grammar: 'for' '(' init ';' condition ';' increment ')' body  OR  'for' '(' decl ':' expr ')' body
	struct ForLoopNode : public AstNode
	{
		ForLoopNode() = default;

		bool IsRangeBased() const { return m_IsRangeBased; }
		const Token& GetForToken() const { return m_ForToken; }
		const Ref<AstNode>& GetInitClause() const { return m_InitClause; }
		const Ref<AstNode>& GetCondition() const { return m_Condition; }
		/// The step clause: expressions separated by commas, run in order after each iteration (D-40).
		const Core::Vector<Ref<AstNode>>& GetSteps() const { return m_Steps; }
		const Ref<CodeBlockNode>& GetBody() const { return m_Body; }
		const Ref<AstNode>& GetSingleStatementBody() const { return m_SingleStatementBody; }

		// Range-based form: for (Type x : expr). The type is in GetInitClause(),
		// the loop variable name here, and the range expression in GetRangeExpression().
		const Token& GetRangeVarNameToken() const { return m_RangeVarNameToken; }
		const Ref<AstNode>& GetRangeExpression() const { return m_Condition; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override { return "for"; }
static Ref<ForLoopNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::ForLoop; }

	private:
		bool m_IsRangeBased = false;
		Token m_ForToken;
		Token m_OpenParenToken;
		Ref<AstNode> m_InitClause; // nullptr if empty or range-based (for range-based, stores type)
		Token m_RangeVarNameToken; // variable name for range-based for
		Token m_FirstSemicolon;
		Token m_ColonToken;        // ':' for range-based for
		Ref<AstNode> m_Condition;  // nullptr if empty (or range expr for range-based)
		Token m_SecondSemicolon;
		Core::Vector<Ref<AstNode>> m_Steps; // empty if there is no step
		Token m_CloseParenToken;
		Ref<CodeBlockNode> m_Body;
		Ref<AstNode> m_SingleStatementBody;
		Core::Vector<Ref<AstNode>> m_Children;
	};
}
