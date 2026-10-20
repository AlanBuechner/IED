#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	struct CodeBlockNode;
	struct QualTypeNode;

	/// One catch clause: 'catch' '(' type [identifier] ')' body
	struct CatchClauseNode : public AstNode
	{
		CatchClauseNode() = default;

		const Ref<QualTypeNode>& GetExceptionType() const { return m_ExceptionType; }
		const Token& GetExceptionNameToken() const { return m_ExceptionNameToken; }
		bool HasExceptionName() const { return !m_ExceptionNameToken.GetText().empty(); }
		bool IsCatchAll() const { return m_IsCatchAll; }
		const Ref<CodeBlockNode>& GetBody() const { return m_Body; }
		const Ref<AstNode>& GetSingleStatementBody() const { return m_SingleStatementBody; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::CatchClause; }

	private:
		Token m_CatchToken;
		Token m_OpenParenToken;
		Ref<QualTypeNode> m_ExceptionType;
		Token m_ExceptionNameToken; // default-constructed if unnamed
		bool m_IsCatchAll = false;   // `catch (...)`
		Token m_CloseParenToken;
		Ref<CodeBlockNode> m_Body;
		Ref<AstNode> m_SingleStatementBody;
		Core::Vector<Ref<AstNode>> m_Children;

		friend struct TryCatchNode;
	};

	/// Try-catch-finally: 'try' body catch_clause* ['finally' body]
	struct TryCatchNode : public AstNode
	{
		TryCatchNode() = default;

		const Ref<CodeBlockNode>& GetTryBody() const { return m_TryBody; }
		const Core::Vector<Ref<CatchClauseNode>>& GetCatchClauses() const { return m_CatchClauses; }
		const Ref<CodeBlockNode>& GetFinallyBody() const { return m_FinallyBody; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override { return "try-catch"; }

		static Ref<TryCatchNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::TryCatch; }

	private:
		Token m_TryToken;
		Ref<CodeBlockNode> m_TryBody;
		Core::Vector<Ref<CatchClauseNode>> m_CatchClauses;
		Ref<CodeBlockNode> m_FinallyBody;
		Core::Vector<Ref<AstNode>> m_Children; // try body + catches + finally
	};
}
