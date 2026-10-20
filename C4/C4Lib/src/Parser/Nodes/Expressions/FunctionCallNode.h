#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	struct TemplateArgumentListNode;

	/// Function/method call expression: callee(arg1, arg2, ...).
	struct FunctionCallNode : public AstNode
	{
		FunctionCallNode() = default;

		const Ref<AstNode>& GetCallee() const { return m_Callee; }
		const Token& GetOpenParenToken() const { return m_OpenParenToken; }
		const Core::Vector<Ref<AstNode>>& GetArguments() const { return m_Arguments; }
		const Token& GetCloseParenToken() const { return m_CloseParenToken; }
		uint32 ArgumentCount() const { return m_Arguments.Count(); }

		/// Explicit template arguments for a function-template call (`Foo<int>(...)`).
		/// nullptr when the callee is not an explicit specialization.
		bool HasTemplateArgs() const { return m_TemplateArgs != nullptr; }
		const Ref<TemplateArgumentListNode>& GetTemplateArgs() const { return m_TemplateArgs; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::FunctionCall; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_Callee;
		Token m_OpenParenToken;
		Core::Vector<Ref<AstNode>> m_Arguments;
		Token m_CloseParenToken;
		Ref<TemplateArgumentListNode> m_TemplateArgs; // nullptr if absent
		mutable Core::Vector<Ref<AstNode>> m_Children;
	};
}
