#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Kind of template parameter.
	enum class TemplateParameterKind
	{
		Type,             // typename/class T
		NonType,          // uint32 I
		VariadicType,     // ...T or ...typename T
		VariadicNonType   // ...uint32 I
	};

	/// A single template parameter inside a template definition.
	///
	/// Examples: `T`, `typename U`, `class V`, `uint32 N`, `...Ts`
	struct TemplateParameterNode : public AstNode
	{
		TemplateParameterNode() = default;

		// --- Getters ---

		const Token& GetNameToken() const { return m_NameToken; }
		const Core::String& GetName() const { return m_NameToken.GetText(); }
		const Token& GetKeywordToken() const { return m_KeywordToken; }
		TemplateParameterKind Kind() const { return m_Kind; }
		const Ref<AstNode>& GetTypeNode() const { return m_TypeNode; }
		/// `= default`: a QualTypeNode for a type parameter, an expression for a value parameter; null if none.
		const Ref<AstNode>& GetDefault() const { return m_Default; }

		static Ref<TemplateParameterNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::TemplateParameter; }

	private:
		void ParseDefault(ParserState& state);

		Token m_KeywordToken;           // 'typename', 'class', or type specifier token for non-type
		Token m_NameToken;              // parameter identifier
		Ref<AstNode> m_TypeNode;        // nullptr for pure type params, set for non-type params
		TemplateParameterKind m_Kind = TemplateParameterKind::Type;
		Ref<AstNode> m_Default;
	};
}
