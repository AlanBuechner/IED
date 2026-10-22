#pragma once
#include <vector>
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"
#include "NamespaceQualifiedNameNode.h"
#include "TemplateDefinitionNode.h"
#include "ParameterNode.h"

namespace Scripting
{
	struct CodeBlockNode;

	/// Trailing qualifiers for member functions.
	/// Represents a function declaration or definition including optional annotations,
	/// access modifiers, static/virtual/override/final/abstract/concurrent qualifiers,
	/// return type, namespace-qualified name, template definition, parameter list,
	/// trailing qualifiers, optional extends clause for extension methods, and optional body.
	///
	/// Grammar: annotation* [access_modifier] [static] [virtual] ... return_type qualified_name [template_def] '(' params ')' [qualifier] [extends Type] (body | ';')
	/// Examples: `void DoThing();`, `public static float NC::SomeClass::DoThing(int i) { }`
	/// Extension: `void Add(const Point& other) extends Point { x += other.x; }`
	struct FunctionDefinitionNode : public AstNode
	{
		FunctionDefinitionNode() = default;

		// --- Getters ---

		const Core::Vector<Ref<AnnotationNode>>& GetAnnotations() const { return m_Annotations; }
		AccessModifier GetAccessModifier() const { return m_AccessModifier; }
		void SetAccessModifier(AccessModifier access) { m_AccessModifier = access; }
		const Token& GetAccessModifierToken() const { return m_AccessModifierToken; }

		bool IsStatic() const { return m_IsStatic; }
		const Token& GetStaticToken() const { return m_StaticToken; }

		/// `abstract` declares a method with no implementation here: it is virtual and pure.
		bool IsVirtual() const { return m_IsVirtual || m_IsAbstract; }
		const Token& GetVirtualToken() const { return m_VirtualToken; }

		bool IsOverride() const { return m_IsOverride; }
		const Token& GetOverrideToken() const { return m_OverrideToken; }

		bool IsFinal() const { return m_IsFinal; }
		const Token& GetFinalToken() const { return m_FinalToken; }

		bool IsAbstract() const { return m_IsAbstract; }
		const Token& GetAbstractToken() const { return m_AbstractToken; }
		/// `inline` (docs/language/07): every call of the function is replaced by its body, at every optimization level.
		bool IsInline() const { return m_IsInline; }
		const Token& GetInlineToken() const { return m_InlineToken; }

		const Ref<QualTypeNode>& GetReturnType() const { return m_ReturnType; }
		const Ref<NamespaceQualifiedNameNode>& GetName() const { return m_Name; }
		const Ref<TemplateDefinitionNode>& GetTemplateDefinition() const { return m_TemplateDefinition; }
		bool IsTemplated() const { return m_TemplateDefinition != nullptr; }
		/// The class template's header of an out-of-line member template (`template<T>` in
		/// `template<T> template<U> R S<T>::F(U u)`); null otherwise.
		const Ref<TemplateDefinitionNode>& GetOwnerTemplateDefinition() const { return m_OwnerTemplateDefinition; }

		const Token& GetOpenParenToken() const { return m_OpenParenToken; }
		const Core::Vector<Ref<ParameterNode>>& GetParameters() const { return m_Parameters; }
		/// How many parameters at the end have a default value: a call may leave them out.
		uint32 TrailingDefaultCount() const;
		uint32 ParameterCount() const { return m_Parameters.Count(); }
		bool IsVariadic() const { return m_IsVariadic; }
		/// Each parameter declared const (D-30), in order.
		std::vector<bool> ParameterConstness() const;
		const Token& GetCloseParenToken() const { return m_CloseParenToken; }

		/// Trailing qualifiers, independent of each other: `int32 Get() const noexcept`.
		bool IsConstMethod() const { return m_IsConstMethod; }
		bool IsNoexcept() const { return m_IsNoexcept; }
		bool IsPureVirtual() const { return m_IsPureVirtual || m_IsAbstract; }
		const Token& GetConstToken() const { return m_ConstToken; }
		const Token& GetNoexceptToken() const { return m_NoexceptToken; }

		bool IsDefinition() const { return m_Body != nullptr; }
		bool IsDeclaration() const { return m_Body == nullptr; }
		const Ref<CodeBlockNode>& GetBody() const { return m_Body; }
		const Token& GetSemicolonToken() const { return m_SemicolonToken; }

		// Extension method support
		bool IsExtensionMethod() const { return m_ExtendsTypeName != nullptr; }
		const Ref<NamespaceQualifiedNameNode>& GetExtendsTypeName() const { return m_ExtendsTypeName; }
		const Token& GetExtendsKeywordToken() const { return m_ExtendsKeywordToken; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
		static Ref<FunctionDefinitionNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::FunctionDefinition; }

	private:
		void ParseTemplateHeaders(ParserState& state, TypeParameterScope& typeParameters);
		void CheckAbstract(ErrorTracker& errorTracker) const;

		Core::Vector<Ref<AnnotationNode>> m_Annotations;
		AccessModifier m_AccessModifier = AccessModifier::None;
		Token m_AccessModifierToken;

		bool m_IsStatic = false;
		Token m_StaticToken;

		bool m_IsVirtual = false;
		Token m_VirtualToken;

		bool m_IsOverride = false;
		Token m_OverrideToken;

		bool m_IsFinal = false;
		Token m_FinalToken;

		bool m_IsAbstract = false;
		Token m_AbstractToken;
		bool m_IsInline = false;
		Token m_InlineToken;

		Ref<QualTypeNode> m_ReturnType;

		Ref<NamespaceQualifiedNameNode> m_Name;
		Ref<TemplateDefinitionNode> m_TemplateDefinition;
		Ref<TemplateDefinitionNode> m_OwnerTemplateDefinition;

		Token m_OpenParenToken;
		Core::Vector<Ref<ParameterNode>> m_Parameters;
		bool m_IsVariadic = false;
		Token m_VariadicToken;
		Token m_CloseParenToken;

		bool m_IsConstMethod = false;
		bool m_IsNoexcept = false;
		bool m_IsPureVirtual = false;
		Token m_ConstToken;
		Token m_NoexceptToken;
		Token m_PureVirtualToken; // '=' for pure virtual

		// Extension method target type: `void Add(P) extends Point { }`
		Token m_ExtendsKeywordToken;
		Ref<NamespaceQualifiedNameNode> m_ExtendsTypeName;

		Ref<CodeBlockNode> m_Body;
		Token m_SemicolonToken;

		Core::Vector<Ref<AstNode>> m_Children; // annotations + returnType + name + templateDef + params + body
	};
}
