#include "AliasDefinitionNode.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"
#include "TemplateDefinitionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	Core::String AliasDefinitionNode::ToString() const
	{
		Core::String result = "alias ";
		if (!m_NameToken.GetText().empty())
			result += m_NameToken.GetText();
		result += " = ";
		if (m_AliasedType)
			result += m_AliasedType->ToString();
		return result;
	}

	std::span<const Ref<AstNode>> AliasDefinitionNode::Children() const
	{
		return m_Children;
	}

	Ref<AliasDefinitionNode> AliasDefinitionNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token first = lexer.PeekCompound();
		if (first.GetType() != TokenType::OpenBracket &&
			first.GetType() != TokenType::Alias &&
			first.GetType() != TokenType::Template &&
			!(first.GetType() == TokenType::Public || first.GetType() == TokenType::Private || first.GetType() == TokenType::Protected))
		{
			return nullptr;
		}

		auto undo = LexerUndoScope(lexer);
		state.GetErrorTracker().SuppressErrors();

		// Parse the full alias definition (annotations, access modifier, optional template
		// prefix, 'alias' keyword, name, '=', type, ';') in one pass. Mirrors
		// FunctionDefinitionNode::TryParse - pre-consuming the template prefix here would
		// leave ParseBody unable to attach it.
		auto node = CreateRef<AliasDefinitionNode>();
		node->ParseBody(state);

		if (node->m_NameToken.GetText().empty() ||
			node->m_AliasKeywordToken.GetType() != TokenType::Alias)
		{
			state.GetErrorTracker().DiscardSuppressedErrors();
			return nullptr;
		}

		state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return node;
	}

	void AliasDefinitionNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_Annotations.Clear();
		m_AccessModifier = AccessModifier::None;
		m_TemplateDefinition = nullptr;
		m_AliasedType = nullptr;

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

		// 3. Optional template definition
		if (next.GetType() == TokenType::Template)
		{
			m_TemplateDefinition = CreateRef<TemplateDefinitionNode>();
			m_TemplateDefinition->ParseBody(state);
			next = lexer.PeekCompound();
		}

		// 4. alias keyword
		if (next.GetType() != TokenType::Alias)
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected 'alias' keyword, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}

		m_AliasKeywordToken = next;
		if (startLocation.line == 0)
			startLocation = next.GetStartLocation();
		lexer.NextCompoundToken();

		// 5. Name
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
				Core::StringFormat::Format("Expected alias name, got '{0}'", nameToken.GetText()),
				ErrorSeverity::Error);
			return;
		}

		// 6. '=' assignment operator
		Token eq = lexer.PeekCompound();
		if (eq.GetType() == TokenType::Equal)
		{
			m_AssignmentToken = eq;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				eq.GetStartLocation(),
				Core::StringFormat::Format("Expected '=' after alias name, got '{0}'", eq.GetText()),
				ErrorSeverity::Error);
			return;
		}

		// 7. Aliased type
		auto aliasedType = CreateRef<QualTypeNode>();
		aliasedType->ParseBody(state);
		m_AliasedType = aliasedType;

		// 8. Semicolon
		Token semi = lexer.PeekCompound();
		if (semi.GetType() == TokenType::SemiColon)
		{
			m_SemicolonToken = semi;
			endLocation = semi.GetEndLocation();
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				semi.GetStartLocation(),
				Core::StringFormat::Format("Expected ';' after alias definition, got '{0}'", semi.GetText()),
				ErrorSeverity::Error);
		}

		if (endLocation.line == 0)
			endLocation = startLocation;

		m_Children.Clear();
		for (uint32 i = 0; i < m_Annotations.Count(); ++i)
			m_Children.Push(m_Annotations[i]);
		if (m_TemplateDefinition)
			m_Children.Push(m_TemplateDefinition);
		if (m_AliasedType)
			m_Children.Push(m_AliasedType);
	}

} // namespace Scripting
