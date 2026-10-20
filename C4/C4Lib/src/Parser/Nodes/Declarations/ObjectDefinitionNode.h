#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	struct AnnotationNode;
	struct QualTypeNode;
	struct TemplateDefinitionNode;
	struct TemplateArgumentListNode;

	enum class ObjectKind { Class, Struct, Interface };

	/// Per-base access modifier for inheritance list.
	enum class InheritanceModifier { Public, Private, Protected, Default };

	/// One base class/interface in an inheritance list.
	struct BaseClassEntry
	{
		InheritanceModifier m_AccessModifier = InheritanceModifier::Default;
		Token m_AccessModifierToken;
		Ref<QualTypeNode> m_Type;
	};

	/// Represents a class, struct, or interface definition including optional annotations,
	/// access modifiers, template definition, name, inheritance list, and body members.
	struct ObjectDefinitionNode : public AstNode
	{
		ObjectDefinitionNode() = default;

		const Core::Vector<Ref<AnnotationNode>>& GetAnnotations() const { return m_Annotations; }
		AccessModifier GetAccessModifier() const { return m_AccessModifier; }
		const Token& GetAccessModifierToken() const { return m_AccessModifierToken; }

		bool IsAbstract() const { return m_IsAbstract; }
		bool IsFinal() const { return m_IsFinal; }
		ObjectKind Kind() const { return m_Kind; }
		const Token& GetObjectKindToken() const { return m_ObjectKindToken; }

		const Token& GetNameToken() const { return m_NameToken; }
		/// True when the body was opened. Anonymous object definitions (`struct { ... };`, used for
		/// swizzle views inside an anonymous union) have an empty name, so TryParse accepts them on
		/// this signal instead of the name.
		bool HasBody() const { return m_HasOpenBrace; }
		/// True for a declaration-only forward declaration (`class Window;`) - no body, so it is
		/// neither an anonymous definition (HasBody) nor a full type definition.
		bool IsDeclaration() const { return m_IsDeclaration; }
		const Ref<TemplateDefinitionNode>& GetTemplateDefinition() const { return m_TemplateDefinition; }
		bool IsTemplated() const { return m_TemplateDefinition != nullptr; }
		/// `template<T> struct Box<T*> { ... }`: the arguments a specialization is for (D-20); null otherwise.
		const Ref<TemplateArgumentListNode>& GetSpecializationArguments() const { return m_SpecializationArguments; }
		bool IsSpecialization() const { return m_SpecializationArguments != nullptr; }

		bool HasInheritanceList() const { return m_HasInheritanceList; }
		const Core::Vector<BaseClassEntry>& GetBaseClasses() const { return m_BaseClasses; }
		uint32 BaseClassCount() const { return m_BaseClasses.Count(); }

		const Core::Vector<Ref<AstNode>>& GetBodyMembers() const { return m_BodyMembers; }
		uint32 MemberCount() const { return m_BodyMembers.Count(); }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
		static Ref<ObjectDefinitionNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::ObjectDefinition; }

	private:
		void ParseClassModifiers(ParserState& state);
		void CheckClassModifiers(ErrorTracker& errorTracker) const;

		Core::Vector<Ref<AnnotationNode>> m_Annotations;
		AccessModifier m_AccessModifier = AccessModifier::None;
		Token m_AccessModifierToken;

		bool m_IsAbstract = false;
		Token m_AbstractToken;
		bool m_IsFinal = false;
		Token m_FinalToken;

		ObjectKind m_Kind = ObjectKind::Class;
		Token m_ObjectKindToken;

		Token m_NameToken;
		Ref<TemplateDefinitionNode> m_TemplateDefinition;
		Ref<TemplateArgumentListNode> m_SpecializationArguments;

		bool m_HasInheritanceList = false;
		Token m_ColonToken;
		Core::Vector<BaseClassEntry> m_BaseClasses;

		Token m_OpenBraceToken;
		bool m_HasOpenBrace = false;
		// True for a declaration-only forward declaration (`class Window;`). It has no body, so it
		// must not be treated as an anonymous definition (HasBody) nor as a full type definition.
		bool m_IsDeclaration = false;
	Core::Vector<Ref<AstNode>> m_BodyMembers;
	Token m_CloseBraceToken;
	Token m_SemicolonToken; // trailing semicolon after class body

	Core::Vector<Ref<AstNode>> m_Children; // annotations + templateDef + baseTypes + bodyMembers
	};
}
