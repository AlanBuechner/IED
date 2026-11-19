#include "AnnotationDeclarationNode.h"
#include "Parser/UnexpectedToken.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	namespace
	{
		bool IsWord(const Token& token) { return token.GetType() == TokenType::Identifier || Token::IsKeyword(token.GetType()); }

		bool EndsDeclaration(TokenType type) { return type == TokenType::SemiColon || type == TokenType::EndOfFile; }

		// After an error: the rest of the declaration says nothing more, and a `}` it lacks a `;` before stays the block's.
		void Recover(Lexer& lexer)
		{
			if (lexer.PeekCompound().GetType() != TokenType::CloseCurl) SkipRestOfStatement(lexer);
		}
	}

	Core::String AnnotationDeclarationNode::ToString() const
	{
		Core::String text = Core::String(kAnnotationKeyword) + " " + GetName();
		if (!m_Parameters.Empty())
		{
			text += "(";
			for (uint32 i = 0; i < m_Parameters.Count(); ++i) text += (i > 0 ? ", " : "") + m_Parameters[i]->ToString();
			text += ")";
		}
		for (uint32 i = 0; i < m_PlaceTokens.Count(); ++i) text += (i > 0 ? ", " : Core::String(" ") + kAnnotationPlacesKeyword + " ") + m_PlaceTokens[i].GetText();
		return text;
	}

	bool AnnotationDeclarationNode::StartsHere(const Lexer& lexer)
	{
		const Token& first = lexer.PeekCompound();
		return first.GetType() == TokenType::Identifier && first.GetText() == kAnnotationKeyword && lexer.PeekCompound(1).GetType() == TokenType::Identifier;
	}

	Ref<AnnotationDeclarationNode> AnnotationDeclarationNode::TryParse(ParserState& state)
	{
		if (!StartsHere(state.GetLexer())) return nullptr;
		auto node = CreateRef<AnnotationDeclarationNode>();
		node->ParseBody(state);
		return node;
	}

	void AnnotationDeclarationNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		m_KeywordToken = lexer.PeekCompound();
		startLocation = m_KeywordToken.GetStartLocation();
		endLocation = m_KeywordToken.GetEndLocation();
		lexer.NextCompoundToken();
		m_NameToken = lexer.PeekCompound();
		endLocation = m_NameToken.GetEndLocation();
		lexer.NextCompoundToken();
		if (lexer.PeekCompound().GetType() == TokenType::OpenParen && !ParseParameters(state)) return Recover(lexer);
		const Token& next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Identifier && next.GetText() == kAnnotationPlacesKeyword && !ParsePlaces(state)) return Recover(lexer);
		if (!ParseEnd(state)) Recover(lexer);
	}

	bool AnnotationDeclarationNode::ParseParameters(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		m_OpenParenToken = lexer.PeekCompound();
		lexer.NextCompoundToken();
		ParameterNode::SkipVoidList(lexer);
		while (lexer.PeekCompound().GetType() != TokenType::CloseParen && !EndsDeclaration(lexer.PeekCompound().GetType()) && ParseParameter(state))
			if (lexer.PeekCompound().GetType() == TokenType::Comma) lexer.NextCompoundToken();
		const Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseParen)
		{
			state.GetErrorTracker().AddError(close.GetStartLocation(),
				Core::StringFormat::Format("Expected ')' to close the parameters of [{0}], got '{1}'", GetName(), close.GetText()), ErrorSeverity::Error);
			return false;
		}
		endLocation = close.GetEndLocation();
		lexer.NextCompoundToken();
		return true;
	}

	bool AnnotationDeclarationNode::ParseParameter(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		const Token at = lexer.PeekCompound();
		if (at.GetType() == TokenType::Ellipsis)
		{
			state.GetErrorTracker().AddError(at.GetStartLocation(),
				"an annotation takes no C '...': name what it takes as a pack (`typename... more`, `const char*... names`)", ErrorSeverity::Error);
			lexer.NextCompoundToken();
			return true;
		}
		Ref<ParameterNode> parameter = ParameterNode::TryParse(state);
		if (!parameter)
		{
			state.GetErrorTracker().AddError(at.GetStartLocation(), Core::StringFormat::Format("Expected a parameter, got '{0}'", at.GetText()), ErrorSeverity::Error);
			return false;
		}
		m_Parameters.Push(parameter);
		m_Children.Push(parameter);
		return true;
	}

	bool AnnotationDeclarationNode::ParsePlaces(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		m_OnToken = lexer.PeekCompound();
		endLocation = m_OnToken.GetEndLocation();
		lexer.NextCompoundToken();
		while (true)
		{
			const Token place = lexer.PeekCompound();
			if (!IsWord(place))
			{
				state.GetErrorTracker().AddError(place.GetStartLocation(),
					Core::StringFormat::Format("Expected a place [{0}] goes (type, field, ...), got '{1}'", GetName(), place.GetText()), ErrorSeverity::Error);
				return false;
			}
			m_PlaceTokens.Push(place);
			endLocation = place.GetEndLocation();
			lexer.NextCompoundToken();
			if (lexer.PeekCompound().GetType() != TokenType::Comma) return true;
			lexer.NextCompoundToken();
		}
	}

	bool AnnotationDeclarationNode::ParseEnd(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		const Token end = lexer.PeekCompound();
		if (end.GetType() == TokenType::SemiColon)
		{
			m_SemicolonToken = end;
			endLocation = end.GetEndLocation();
			lexer.NextCompoundToken();
			return true;
		}
		const char* expected = !m_OnToken.GetText().empty() ? "',' or ';'" : m_OpenParenToken.GetText().empty() ? "'(', 'on' or ';'" : "'on' or ';'";
		state.GetErrorTracker().AddError(end.GetStartLocation(),
			Core::StringFormat::Format("Expected {0} after the declaration of [{1}], got '{2}'", expected, GetName(), end.GetText()), ErrorSeverity::Error);
		return false;
	}
} // namespace Scripting
