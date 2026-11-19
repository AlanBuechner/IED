#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// `annotation` starts an annotation declaration at namespace scope; anywhere else it is a name like any other.
	constexpr const char* kAnnotationKeyword = "annotation";
	/// `on` starts the places an annotation goes, after its parameters.
	constexpr const char* kAnnotationPlacesKeyword = "on";

	/// Declares an annotation, what it takes and where it goes (docs/language/12 section 4):
	/// `annotation Range(double min, double max) on field, parameter;`.
	///
	/// Grammar: 'annotation' identifier ['(' [parameter {',' parameter}] ')'] ['on' place {',' place}] ';'
	struct AnnotationDeclarationNode : public AstNode
	{
		AnnotationDeclarationNode() = default;

		const Token& GetKeywordToken() const { return m_KeywordToken; }
		const Token& GetNameToken() const { return m_NameToken; }
		const Core::String& GetName() const { return m_NameToken.GetText(); }

		/// Its parameters, the annotation's arguments; none when no parentheses are written.
		const Core::Vector<Ref<ParameterNode>>& GetParameters() const { return m_Parameters; }
		/// `on`, or an empty token when the annotation goes anywhere.
		const Token& GetOnToken() const { return m_OnToken; }
		/// The places after `on` as written (`field`, `enum`); the model says which name none.
		const Core::Vector<Token>& GetPlaceTokens() const { return m_PlaceTokens; }
		/// The `;` that ends it, or an empty token when it is missing.
		const Token& GetSemicolonToken() const { return m_SemicolonToken; }

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;

		/// Whether the lexer is at an annotation declaration: `annotation` followed by a name.
		static bool StartsHere(const Lexer& lexer);
		static Ref<AnnotationDeclarationNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::AnnotationDeclaration; }

	private:
		bool ParseParameters(ParserState& state);
		bool ParseParameter(ParserState& state);
		bool ParsePlaces(ParserState& state);
		bool ParseEnd(ParserState& state);

		Token m_KeywordToken;
		Token m_NameToken;
		Token m_OpenParenToken;
		Core::Vector<Ref<ParameterNode>> m_Parameters;
		Token m_OnToken;
		Core::Vector<Token> m_PlaceTokens;
		Token m_SemicolonToken;
		Core::Vector<Ref<AstNode>> m_Children; // the parameters
	};
} // namespace Scripting
