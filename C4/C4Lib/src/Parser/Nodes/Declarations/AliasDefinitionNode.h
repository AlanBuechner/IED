#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"

namespace Scripting
{
	struct TemplateDefinitionNode;

	/// Type alias definition with optional annotations, access modifier, template definition, and aliased type.
	/// Grammar: annotation* [access_modifier] [template_definition] 'alias' identifier '=' type ';'
	struct AliasDefinitionNode : public AstNode
	{
		AliasDefinitionNode() = default;

		const Core::Vector<Ref<AnnotationNode>>& GetAnnotations() const { return m_Annotations; }
		AccessModifier GetAccessModifier() const { return m_AccessModifier; }
		void SetAccessModifier(AccessModifier access) { m_AccessModifier = access; }

		const Ref<TemplateDefinitionNode>& GetTemplateDefinition() const { return m_TemplateDefinition; }
		bool IsTemplated() const { return m_TemplateDefinition != nullptr; }

		const Token& GetAliasKeywordToken() const { return m_AliasKeywordToken; }
		const Token& GetNameToken() const { return m_NameToken; }

		const Ref<QualTypeNode>& GetAliasedType() const { return m_AliasedType; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;

		static Ref<AliasDefinitionNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::AliasDefinition; }

	private:
		Core::Vector<Ref<AnnotationNode>> m_Annotations;
		AccessModifier m_AccessModifier = AccessModifier::None;
		Token m_AccessModifierToken;

		Ref<TemplateDefinitionNode> m_TemplateDefinition;

		Token m_AliasKeywordToken;
		Token m_NameToken;
		Token m_AssignmentToken;

		Ref<QualTypeNode> m_AliasedType;
		Token m_SemicolonToken;

		Core::Vector<Ref<AstNode>> m_Children; // annotations + templateDef + aliasedType
	};
}
