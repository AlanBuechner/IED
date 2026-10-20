#include "EnumDefinitionNode.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"
#include "../Expressions/LiteralNode.h"
#include "../Expressions/IdentifierExpressionNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	Core::String EnumMemberNode::ToString() const
	{
		if (HasInitializer())
			return Core::StringFormat::Format("{0} = {1}", m_NameToken.GetText(), m_Initializer->ToString());
		return m_NameToken.GetText();
	}

	Core::String EnumDefinitionNode::ToString() const
	{
		Core::String result;
		if (m_AccessModifier == AccessModifier::Public)
			result += "public ";
		result += "enum ";
		if (!m_NameToken.GetText().empty())
			result += m_NameToken.GetText();

		if (m_HasUnderlyingType && m_UnderlyingType)
		{
			result += " : ";
			result += m_UnderlyingType->ToString();
		}
		return result;
	}

	std::span<const Ref<AstNode>> EnumDefinitionNode::Children() const
	{
		return m_Children;
	}

	Ref<EnumDefinitionNode> EnumDefinitionNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

	auto undo = LexerUndoScope(lexer);
	state.GetErrorTracker().SuppressErrors();

	if (AnnotationNode::PeekPastAnnotations(state) != TokenType::Enum)
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	auto node = CreateRef<EnumDefinitionNode>();
	node->ParseBody(state);

	if (!node->m_NameToken.GetText().empty())
	{
		state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return node;
	}

	state.GetErrorTracker().DiscardSuppressedErrors();
	return nullptr;
	}

	void EnumDefinitionNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_Annotations.Clear();
		m_AccessModifier = AccessModifier::None;
		m_HasUnderlyingType = false;
		m_IsScoped = false;
		m_Members.Clear();

		// 1. Parse annotations
		while (true)
		{
			Token t = lexer.PeekCompound();
			if (t.GetType() != TokenType::OpenBracket)
				break;

			auto annotation = CreateRef<AnnotationNode>();
			annotation->ParseBody(state);

			if (!annotation->GetBracketGroups().Empty())
			{
				m_Annotations.Push(annotation);
				if (startLocation.line == 0)
					startLocation = annotation->startLocation;
			}
			else
			{
				errorTracker.AddError(t.GetStartLocation(), "Expected valid annotation", ErrorSeverity::Error);
				break;
			}
		}

		// 2. Access modifier
		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Public || next.GetType() == TokenType::Private || next.GetType() == TokenType::Protected)
		{
			m_AccessModifier = AccessModifierOf(next.GetType());
			m_AccessModifierToken = next;
			if (startLocation.line == 0)
				startLocation = next.GetStartLocation();
			lexer.NextCompoundToken();
			next = lexer.PeekCompound();
		}

		// 3. enum keyword
		if (next.GetType() != TokenType::Enum)
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected 'enum' keyword, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}

		m_EnumKeywordToken = next;
		if (startLocation.line == 0)
			startLocation = next.GetStartLocation();
		lexer.NextCompoundToken();

		// 4. Optional 'class'/'struct' keyword (scoped enum), then the name
		Token scopedToken = lexer.PeekCompound();
		if (scopedToken.GetType() == TokenType::Class || scopedToken.GetType() == TokenType::Struct)
		{
			m_IsScoped = true;
			lexer.NextCompoundToken();
		}

		Token nameToken = lexer.PeekCompound();
		if (nameToken.GetType() == TokenType::Identifier)
		{
			m_NameToken = nameToken;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				nameToken.GetStartLocation(),
				Core::StringFormat::Format("Expected enum name, got '{0}'", nameToken.GetText()),
				ErrorSeverity::Error);
			return;
		}

		// 5. Optional underlying type after ':'
		Token maybeColon = lexer.PeekCompound();
		if (maybeColon.GetType() == TokenType::Colon)
		{
			m_HasUnderlyingType = true;
			m_ColonToken = maybeColon;
			lexer.NextCompoundToken();

			auto underlyingType = CreateRef<QualTypeNode>();
			underlyingType->ParseBody(state);
			m_UnderlyingType = underlyingType;
		}

		// 6. Opening brace
		Token openBrace = lexer.PeekCompound();
		if (openBrace.GetType() == TokenType::OpenCurl)
		{
			m_OpenBraceToken = openBrace;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				openBrace.GetStartLocation(),
				Core::StringFormat::Format("Expected an opening brace for the enum body, got '{0}'", openBrace.GetText()),
				ErrorSeverity::Error);
			return;
		}

		// 7. Parse members
		while (true)
		{
			Token t = lexer.PeekCompound();

			if (t.GetType() == TokenType::CloseCurl || t.GetType() == TokenType::EndOfFile)
				break;

			if (t.GetType() == TokenType::Comma)
			{
				lexer.NextCompoundToken();
				continue;
			}

			auto member = CreateRef<EnumMemberNode>();
			member->startLocation = t.GetStartLocation();

			if (t.GetType() == TokenType::Identifier)
			{
				member->m_NameToken = t;
				lexer.NextCompoundToken();

				Token afterName = lexer.PeekCompound();
				if (afterName.GetType() == TokenType::Equal)
				{
					member->m_AssignmentToken = afterName;
					lexer.NextCompoundToken();

					// Full expression so initializers like -1, 0x1F, or (A + B) work. The Pratt
					// parser stops at the first non-operator token - the ',' separator or the
					// closing '}' - so it cannot run into the next member.
					member->m_Initializer = PrattParser::ParseExpression(state);
					if (!member->m_Initializer)
					{
						errorTracker.AddError(
							lexer.PeekCompound().GetStartLocation(),
							"Expected initializer expression",
							ErrorSeverity::Error);
					}

					if (member->m_Initializer)
						member->endLocation = member->m_Initializer->endLocation;
				}
				else
				{
					member->endLocation = t.GetEndLocation();
				}

				m_Members.Push(member);
			}
			else
			{
				errorTracker.AddError(
					t.GetStartLocation(),
					Core::StringFormat::Format("Expected enum member name, got '{0}'", t.GetText()),
					ErrorSeverity::Error);
				lexer.NextCompoundToken();
			}
		}

		// 8. Closing brace
		Token closeBrace = lexer.PeekCompound();
		if (closeBrace.GetType() == TokenType::CloseCurl)
		{
			m_CloseBraceToken = closeBrace;
			endLocation = closeBrace.GetEndLocation();
			lexer.NextCompoundToken();
		}
		else if (closeBrace.GetType() != TokenType::EndOfFile)
		{
			errorTracker.AddError(
				closeBrace.GetStartLocation(),
				Core::StringFormat::Format("Expected '}}' to close enum, got '{0}'", closeBrace.GetText()),
				ErrorSeverity::Error);
		}

		// 9. Optional trailing semicolon
		Token semi = lexer.PeekCompound();
		if (semi.GetType() == TokenType::SemiColon)
		{
			m_SemicolonToken = semi;
			endLocation = semi.GetEndLocation();
			lexer.NextCompoundToken();
		}

		if (endLocation.line == 0)
			endLocation = startLocation;

		m_Children.Clear();
		for (uint32 i = 0; i < m_Annotations.Count(); ++i)
			m_Children.Push(m_Annotations[i]);
		if (m_UnderlyingType)
			m_Children.Push(m_UnderlyingType);
		for (uint32 i = 0; i < m_Members.Count(); ++i)
			m_Children.Push(m_Members[i]);
	}

} // namespace Scripting
