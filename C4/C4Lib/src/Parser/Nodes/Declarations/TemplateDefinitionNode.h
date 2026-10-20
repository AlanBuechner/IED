#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"
#include "TemplateParameterNode.h"

namespace Scripting
{
	/// A template definition prefix: `template<T, uint32 I>`.
	/// Wraps a list of TemplateParameterNodes.
	struct TemplateDefinitionNode : public AstNode
	{
		TemplateDefinitionNode() = default;

		// --- Getters ---

		const Token& GetTemplateKeywordToken() const { return m_TemplateKeywordToken; }
		const Token& GetOpenAngleToken() const { return m_OpenAngleToken; }
		const Core::Vector<Ref<AstNode>>& GetParametersAsNodes() const { return m_Parameters; }
		uint32 ParameterCount() const { return m_Parameters.Count(); }
		const Token& GetCloseAngleToken() const { return m_CloseAngleToken; }

		/// Access parameter at index as typed TemplateParameterNode.
		Ref<TemplateParameterNode> GetParameter(uint32 index) const;

		static Ref<TemplateDefinitionNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);
		/// Its type parameters, in scope for the rest of the declaration it heads.
		void BringTypeParametersInto(TypeParameterScope& scope) const;

		std::span<const Ref<AstNode>> Children() const override { return m_Parameters; }
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::TemplateDefinition; }

	private:
		Token m_TemplateKeywordToken;
		Token m_OpenAngleToken;
		Core::Vector<Ref<AstNode>> m_Parameters;
		Token m_CloseAngleToken;
	};
}
