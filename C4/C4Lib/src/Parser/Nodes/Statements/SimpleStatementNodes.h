#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	struct CodeBlockNode;

	/// Do-while loop: 'do' body 'while' '(' expression ')' ';'
	struct DoWhileLoopNode : public AstNode
	{
		DoWhileLoopNode() = default;

		const Ref<CodeBlockNode>& GetBody() const { return m_Body; }
		const Ref<AstNode>& GetSingleStatementBody() const { return m_SingleStatementBody; }
		const Ref<AstNode>& GetCondition() const { return m_Condition; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override { return "do-while"; }
static Ref<DoWhileLoopNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::DoWhileLoop; }

	private:
		Token m_DoToken;
		Ref<CodeBlockNode> m_Body;
		Ref<AstNode> m_SingleStatementBody;
		Token m_WhileToken;
		Token m_OpenParenToken;
		Ref<AstNode> m_Condition;
		Token m_CloseParenToken;
		Token m_SemicolonToken;
		Core::Vector<Ref<AstNode>> m_Children;
	};

	/// Return statement: 'return' [expression] ['{...}'] ';'
	struct ReturnStatementNode : public AstNode
	{
		ReturnStatementNode() = default;

		const Ref<AstNode>& GetExpression() const { return m_Expression; }
		const Ref<AstNode>& GetBracedInitializer() const { return m_BracedInitializer; }
		bool HasValue() const { return m_Expression != nullptr || m_BracedInitializer != nullptr; }

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override { return "return"; }
static Ref<ReturnStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::ReturnStatement; }

	private:
		Token m_ReturnToken;
		Ref<AstNode> m_Expression;
		Ref<AstNode> m_BracedInitializer;
		Token m_SemicolonToken;
		Core::Vector<Ref<AstNode>> m_Children;
	};

	/// Break statement: 'break' ';'
	struct BreakStatementNode : public AstNode
	{
		BreakStatementNode() = default;

		Core::String ToString() const override { return "break"; }

		static Ref<BreakStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::BreakStatement; }

	private:
		Token m_BreakToken;
		Token m_SemicolonToken;
	};

	/// Continue statement: 'continue' ';'
	struct ContinueStatementNode : public AstNode
	{
		ContinueStatementNode() = default;

		Core::String ToString() const override { return "continue"; }

		static Ref<ContinueStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::ContinueStatement; }

	private:
		Token m_ContinueToken;
		Token m_SemicolonToken;
	};

	/// Defer statement: 'defer' body or expression ';'
	struct DeferStatementNode : public AstNode
	{
		DeferStatementNode() = default;

		const Ref<CodeBlockNode>& GetBody() const { return m_Body; }
		const Ref<AstNode>& GetExpression() const { return m_Expression; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override { return "defer"; }
static Ref<DeferStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::DeferStatement; }

	private:
		Token m_DeferToken;
		Ref<CodeBlockNode> m_Body;
		Ref<AstNode> m_Expression; // expression followed by ';' for non-block defers
		Core::Vector<Ref<AstNode>> m_Children;
	};

	/// Fallthrough statement: 'fallthrough' ';'
	struct FallthroughStatementNode : public AstNode
	{
		FallthroughStatementNode() = default;

		Core::String ToString() const override { return "fallthrough"; }

		static Ref<FallthroughStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::FallthroughStatement; }

	private:
		Token m_FallthroughToken;
		Token m_SemicolonToken;
	};

	/// Goto statement: 'goto' identifier ';'
	struct GotoStatementNode : public AstNode
	{
		GotoStatementNode() = default;

		const Token& GetLabelNameToken() const { return m_LabelNameToken; }

		Core::String ToString() const override
		{
			return Core::StringFormat::Format("goto {0}", m_LabelNameToken.GetText());
		}

		static Ref<GotoStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::GotoStatement; }

	private:
		Token m_GotoToken;
		Token m_LabelNameToken;
		Token m_SemicolonToken;
	};

	/// Label statement: identifier ':' statement
	struct LabelStatementNode : public AstNode
	{
		LabelStatementNode() = default;

		const Token& GetLabelNameToken() const { return m_LabelNameToken; }
		const Ref<AstNode>& GetStatement() const { return m_Statement; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override
		{
			return Core::StringFormat::Format("{0}:", m_LabelNameToken.GetText());
		}
static Ref<LabelStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::LabelStatement; }

	private:
		Token m_LabelNameToken;
		Token m_ColonToken;
		Ref<AstNode> m_Statement;
		Core::Vector<Ref<AstNode>> m_Children;
	};

	/// Throw statement: 'throw' expression ';' or bare 'throw;' (rethrow)
	struct ThrowStatementNode : public AstNode
	{
		ThrowStatementNode() = default;

		const Ref<AstNode>& GetExpression() const { return m_Expression; }
		bool IsRethrow() const { return !m_Expression; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override { return "throw"; }
static Ref<ThrowStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::ThrowStatement; }

	private:
		Token m_ThrowToken;
		Ref<AstNode> m_Expression;
		Token m_SemicolonToken;
		Core::Vector<Ref<AstNode>> m_Children;
	};
}
