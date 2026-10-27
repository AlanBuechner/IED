#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"

namespace Scripting
{

	/// One declarator in a variable definition's comma list: `int a = 1, b = 2;` has two.
	struct Declarator
	{
		Token m_NameToken;
		Ref<AstNode> m_Initializer; // optional per-declarator initializer (the '=' form)
		// Direct construction args for `T v(args);` (C++ direct-init). Empty when absent. Outside a function body
		// only a group that cannot be a parameter list is arguments (Declarations/DeclarationShapes), so this
		// cannot steal a prototype like `int32 f(int32 x);`.
		Core::Vector<Ref<AstNode>> m_CtorArgs;
	};

	/// Represents a variable declaration including optional annotations, access modifiers,
	/// static/const/volatile/thread_local qualifiers, type, name, and optional initializer.
	///
	/// Grammar: annotation* [access_modifier] [static] [const] type identifier [= expression] ';'
	/// Examples: `int x;`, `public static const float PI = 3.14f;`, `[Serialize] NC::Vector<int> items;`
	struct VariableDefinitionNode : public AstNode
	{
		VariableDefinitionNode() = default;

		// --- Getters ---

		const Core::Vector<Ref<AnnotationNode>>& GetAnnotations() const { return m_Annotations; }
		AccessModifier GetAccessModifier() const { return m_AccessModifier; }
		void SetAccessModifier(AccessModifier access) { m_AccessModifier = access; }
		const Token& GetAccessModifierToken() const { return m_AccessModifierToken; }

		bool IsStatic() const { return m_IsStatic; }
		const Token& GetStaticToken() const { return m_StaticToken; }

		/// The variable is read-only (D-30): `const int32 n` and `int32 const* p` are; `const int32* p`, a
		/// pointer to const, is not.
		bool IsConst() const { return m_Type ? m_Type->DeclaresConstValue() : m_IsConst; }
		const Token& GetConstToken() const { return m_ConstToken; }

		bool IsVolatile() const { return m_IsVolatile; }
		const Token& GetVolatileToken() const { return m_VolatileToken; }

		bool IsThreadLocal() const { return m_IsThreadLocal; }
		const Token& GetThreadLocalToken() const { return m_ThreadLocalToken; }

	const Ref<QualTypeNode>& GetType() const { return m_Type; }
	const Token& GetNameToken() const { return m_NameToken; }

	/// Comma declarators: `int a = 1, b = 2;` -> [a=1, b=2]. Always contains at least the primary name.
	const Core::Vector<Declarator>& GetDeclarators() const { return m_Declarators; }
	uint32 DeclaratorCount() const { return m_Declarators.Count(); }

		const Ref<AstNode>& GetInitializer() const { return m_Initializer; }
		bool HasInitializer() const { return m_Initializer != nullptr; }

		const Token& GetAssignmentToken() const { return m_AssignmentToken; }
		const Token& GetSemicolonToken() const { return m_SemicolonToken; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
static Ref<VariableDefinitionNode> TryParse(ParserState& state, bool consumeSemicolon = true);
		/// The variable a lambda's init capture declares (`n` of `[n = base + 1]`, D-22): its name and initializer, no
		/// written type. Tools read it; the compiler lowers the capture itself.
		static Ref<VariableDefinitionNode> ForInitCapture(const Token& name, const Token& assignment, const Ref<AstNode>& initializer);
		void ParseBody(ParserState& state, bool consumeSemicolon = true);

	protected:
		NodeType Type() const override { return NodeType::VariableDefinition; }

	private:
		Core::Vector<Ref<AnnotationNode>> m_Annotations;
		AccessModifier m_AccessModifier = AccessModifier::None;
		Token m_AccessModifierToken;

		bool m_IsStatic = false;
		Token m_StaticToken;

		bool m_IsConst = false;
		Token m_ConstToken;

		bool m_IsVolatile = false;
		Token m_VolatileToken;

		bool m_IsThreadLocal = false;
		Token m_ThreadLocalToken;

	Ref<QualTypeNode> m_Type;
	Token m_NameToken;

	Core::Vector<Declarator> m_Declarators; // comma list; [0] mirrors m_NameToken/m_Initializer

	Ref<AstNode> m_Initializer;
		Token m_AssignmentToken;

		Token m_SemicolonToken;
		Core::Vector<Ref<AstNode>> m_Children; // annotations + type + initializer
	};
}
