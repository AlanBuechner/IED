#include "TemplateParameterNode.h"
#include "../Declarations/NamespaceQualifiedNameNode.h"
#include "../Declarations/QualTypeNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{

	Ref<TemplateParameterNode> TemplateParameterNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token first = lexer.PeekCompound();

		if (first.GetType() != TokenType::Ellipsis &&
			first.GetType() != TokenType::Identifier &&
			first.GetType() != TokenType::Class)
		{
			return nullptr;
		}

		if (first.GetType() == TokenType::Identifier)
		{
			if (first.GetText() != "typename" && first.GetText() != "class")
			{
				uint32 peekIndex = 0;
				int segmentCount = 0;

				while (true)
				{
					Token t = lexer.PeekCompound(peekIndex);
					if (t.GetType() == TokenType::EndOfFile || t.GetType() == TokenType::None)
						break;
					if (t.GetType() == TokenType::Comma || t.GetType() == TokenType::Greater)
						break;
					if (t.GetType() == TokenType::Identifier)
						++segmentCount;
					++peekIndex;
				}

				if (segmentCount >= 1)
					return CreateRef<TemplateParameterNode>();
			}
			else
			{
				Token next = lexer.PeekCompound(1);
				if (next.GetType() == TokenType::Identifier)
					return CreateRef<TemplateParameterNode>();
			}
		}
		else if (first.GetType() == TokenType::Class)
		{
			Token next = lexer.PeekCompound(1);
			if (next.GetType() == TokenType::Identifier)
				return CreateRef<TemplateParameterNode>();
		}
		else if (first.GetType() == TokenType::Ellipsis)
		{
			Token next = lexer.PeekCompound(1);
			if (next.GetType() == TokenType::Identifier ||
				next.GetType() == TokenType::Class ||
				(next.GetType() == TokenType::Identifier && (next.GetText() == "typename" || next.GetText() == "class")))
			{
				return CreateRef<TemplateParameterNode>();
			}
		}

		return nullptr;
	}

	void TemplateParameterNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		bool isVariadic = false;
		Token first = lexer.PeekCompound();

		if (first.GetType() == TokenType::Ellipsis)
		{
			isVariadic = true;
			startLocation = first.GetStartLocation();
			lexer.NextCompoundToken();
			first = lexer.PeekCompound();
		}

		if (first.GetType() == TokenType::Class)
		{
			m_KeywordToken = first;
			lexer.NextCompoundToken();
			m_Kind = isVariadic ? TemplateParameterKind::VariadicType : TemplateParameterKind::Type;
		}
		else if (first.GetType() == TokenType::Identifier &&
		         (first.GetText() == "typename" || first.GetText() == "class"))
		{
			m_KeywordToken = first;
			lexer.NextCompoundToken();
			m_Kind = isVariadic ? TemplateParameterKind::VariadicType : TemplateParameterKind::Type;
		}
		else
		{
			Token firstToken = lexer.PeekCompound();
			if (firstToken.GetType() != TokenType::Identifier)
			{
				errorTracker.AddError(
					firstToken.GetStartLocation(),
					Core::StringFormat::Format("Expected template parameter kind ('typename', 'class', or type specifier), got '{0}'", firstToken.GetText()),
					ErrorSeverity::Error);
				if (startLocation.line == 0)
					startLocation = firstToken.GetStartLocation();
				return;
			}

			uint32 peekIndex = 0;
			int segmentCount = 0;
			bool foundEnd = false;

			while (true)
			{
				Token t = lexer.PeekCompound(peekIndex);
				if (t.GetType() == TokenType::EndOfFile || t.GetType() == TokenType::None)
					break;
				if (t.GetType() == TokenType::Comma || t.GetType() == TokenType::Greater || t.GetType() == TokenType::Equal)
				{
					foundEnd = true;
					break;
				}
				if (t.GetType() == TokenType::Identifier)
					++segmentCount;
				++peekIndex;
			}

			if (!foundEnd)
			{
				m_Kind = isVariadic ? TemplateParameterKind::VariadicType : TemplateParameterKind::Type;
			}
			else if (segmentCount <= 1)
			{
				m_Kind = isVariadic ? TemplateParameterKind::VariadicType : TemplateParameterKind::Type;
			}
			else
			{
				auto typeNode = NamespaceQualifiedNameNode::TryParse(state);
				if (typeNode != nullptr)
				{
					m_TypeNode = typeNode;
					m_KeywordToken = typeNode->GetFirstIdentifierToken();
					m_Kind = isVariadic ? TemplateParameterKind::VariadicNonType : TemplateParameterKind::NonType;
					if (startLocation.line == 0)
						startLocation = typeNode->startLocation;
				}
				else
				{
					errorTracker.AddError(
						firstToken.GetStartLocation(),
						Core::StringFormat::Format("Expected valid type specifier for non-type template parameter"),
						ErrorSeverity::Error);
					if (startLocation.line == 0)
						startLocation = firstToken.GetStartLocation();
				}
			}
		}

		Token nameToken = lexer.PeekCompound();
		if (nameToken.GetType() != TokenType::Identifier)
		{
			errorTracker.AddError(
				nameToken.GetStartLocation(),
				Core::StringFormat::Format("Expected identifier for template parameter name, got '{0}'", nameToken.GetText()),
				ErrorSeverity::Error);
			m_NameToken = nameToken;
			endLocation = nameToken.GetEndLocation();
			return;
		}

		m_NameToken = nameToken;
		if (startLocation.line == 0)
			startLocation = nameToken.GetStartLocation();
		lexer.NextCompoundToken();
		endLocation = nameToken.GetEndLocation();
		if (lexer.PeekCompound().GetType() == TokenType::Equal) ParseDefault(state);
	}

	void TemplateParameterNode::ParseDefault(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		lexer.NextCompoundToken(); // '='
		const bool takesType = m_Kind == TemplateParameterKind::Type;
		if (takesType) m_Default = QualTypeNode::TryParse(state);
		else m_Default = PrattParser::ParseTemplateArgumentExpression(state);
		if (!m_Default)
		{
			state.GetErrorTracker().AddError(lexer.PeekCompound().GetStartLocation(),
				Core::StringFormat::Format("Expected a default {0} for template parameter '{1}'", takesType ? "type" : "value", GetName()),
				ErrorSeverity::Error);
			return;
		}
		endLocation = m_Default->endLocation;
	}

	Core::String TemplateParameterNode::ToString() const
	{
		Core::String result;

		if (m_Kind == TemplateParameterKind::VariadicType || m_Kind == TemplateParameterKind::VariadicNonType)
			result += "...";

		switch (m_Kind)
		{
		case TemplateParameterKind::Type:
			if (!m_KeywordToken.GetText().empty())
				result += m_KeywordToken.GetText();
			break;
		case TemplateParameterKind::VariadicType:
			if (!m_KeywordToken.GetText().empty())
				result += m_KeywordToken.GetText();
			else
				result += "typename";
			break;
		case TemplateParameterKind::NonType:
		case TemplateParameterKind::VariadicNonType:
			if (m_TypeNode != nullptr)
				result += m_TypeNode->ToString();
			break;
		}

		if (!result.empty() && !m_NameToken.GetText().empty())
			result += " ";
		result += m_NameToken.GetText();
		if (m_Default) result += " = " + m_Default->ToString();

		return result;
	}

} // namespace Scripting
