#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"
#include "AnnotationNode.h"

namespace Scripting
{
	struct VariableDefinitionNode;

	/// Union type definition with optional annotations, access modifier, and member fields.
	/// Grammar: annotation* [access_modifier] 'union' identifier '{' union_member* '}' ';'
	struct UnionDefinitionNode : public AstNode
	{
		UnionDefinitionNode() = default;

		const Core::Vector<Ref<AnnotationNode>>& GetAnnotations() const { return m_Annotations; }
		AccessModifier GetAccessModifier() const { return m_AccessModifier; }
		void SetAccessModifier(AccessModifier access) { m_AccessModifier = access; }

		const Token& GetUnionKeywordToken() const { return m_UnionKeywordToken; }
		const Token& GetNameToken() const { return m_NameToken; }
		/// True when the union body was opened. Anonymous unions (`union { ... };`) have an empty
		/// name, so TryParse accepts them on this signal instead of the name.
		bool HasBody() const { return m_HasOpenBrace; }

		const Core::Vector<Ref<AstNode>>& GetMembers() const { return m_Members; }
		uint32 MemberCount() const { return m_Members.Count(); }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;

		static Ref<UnionDefinitionNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::UnionDefinition; }

	private:
		Core::Vector<Ref<AnnotationNode>> m_Annotations;
		AccessModifier m_AccessModifier = AccessModifier::None;
		Token m_AccessModifierToken;

		Token m_UnionKeywordToken;
		Token m_NameToken;
		bool m_HasOpenBrace = false;

		Token m_OpenBraceToken;
		Core::Vector<Ref<AstNode>> m_Members;
		Token m_CloseBraceToken;
		Token m_SemicolonToken;

		Core::Vector<Ref<AstNode>> m_Children; // annotations + members
	};
}
