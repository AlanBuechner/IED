#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Core/Core.h"

namespace Scripting
{

	/// One enum member: `Name` or `Name = expr`.
	struct EnumMemberNode : public AstNode
	{
		EnumMemberNode() = default;

		const Token& GetNameToken() const { return m_NameToken; }
		const Ref<AstNode>& GetInitializer() const { return m_Initializer; }
		bool HasInitializer() const { return m_Initializer != nullptr; }

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::EnumMember; }

	private:
		Token m_NameToken;
		Ref<AstNode> m_Initializer;
		Token m_AssignmentToken;

		friend struct EnumDefinitionNode;
	};

	/// Enum type definition with optional annotations, access modifier, underlying type, and members.
	/// Grammar: annotation* [access_modifier] 'enum' identifier [':' type] '{' enum_member (',' enum_member)* '}' ';'
	struct EnumDefinitionNode : public AstNode
	{
		EnumDefinitionNode() = default;

		const Core::Vector<Ref<AnnotationNode>>& GetAnnotations() const { return m_Annotations; }
		AccessModifier GetAccessModifier() const { return m_AccessModifier; }
		void SetAccessModifier(AccessModifier access) { m_AccessModifier = access; }

		const Token& GetEnumKeywordToken() const { return m_EnumKeywordToken; }
		const Token& GetNameToken() const { return m_NameToken; }
		bool IsScoped() const { return m_IsScoped; }

		bool HasUnderlyingType() const { return m_HasUnderlyingType; }
		const Ref<QualTypeNode>& GetUnderlyingType() const { return m_UnderlyingType; }

		const Core::Vector<Ref<EnumMemberNode>>& GetMembers() const { return m_Members; }
		uint32 MemberCount() const { return m_Members.Count(); }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;

		static Ref<EnumDefinitionNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::EnumDefinition; }

	private:
		Core::Vector<Ref<AnnotationNode>> m_Annotations;
		AccessModifier m_AccessModifier = AccessModifier::None;
		Token m_AccessModifierToken;

		Token m_EnumKeywordToken;
		Token m_NameToken;
		bool m_IsScoped = false; // 'enum class' / 'enum struct' - members are not injected into the enclosing scope

		bool m_HasUnderlyingType = false;
		Token m_ColonToken;
		Ref<QualTypeNode> m_UnderlyingType;

		Token m_OpenBraceToken;
		Core::Vector<Ref<EnumMemberNode>> m_Members;
		Token m_CloseBraceToken;
		Token m_SemicolonToken;

		Core::Vector<Ref<AstNode>> m_Children; // annotations + underlyingType + members
	};
}
