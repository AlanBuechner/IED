#include "UnionDefinitionNode.h"
#include "AnnotationNode.h"
#include "VariableDefinitionNode.h"
#include "TemplateDefinitionNode.h"
#include "ObjectDefinitionNode.h"
#include "SwizzleViewNode.h"
#include "../Expressions/LiteralNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	Core::String UnionDefinitionNode::ToString() const
	{
		Core::String result = "union ";
		if (!m_NameToken.GetText().empty())
			result += m_NameToken.GetText();
		return result;
	}

	std::span<const Ref<AstNode>> UnionDefinitionNode::Children() const
	{
		return m_Children;
	}

	Ref<UnionDefinitionNode> UnionDefinitionNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

	auto undo = LexerUndoScope(lexer);
	state.GetErrorTracker().SuppressErrors();

	if (AnnotationNode::PeekPastAnnotations(state) != TokenType::Union)
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	auto node = CreateRef<UnionDefinitionNode>();
	node->ParseBody(state);

	// Named unions succeed on the name; anonymous unions (`union { ... };`) on the opened body.
	if (!node->m_NameToken.GetText().empty() || node->HasBody())
	{
		state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return node;
	}

	state.GetErrorTracker().DiscardSuppressedErrors();
	return nullptr;
	}

	void UnionDefinitionNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_Annotations.Clear();
		m_AccessModifier = AccessModifier::None;
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

		// 3. union keyword
		if (next.GetType() != TokenType::Union)
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected 'union' keyword, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}

		m_UnionKeywordToken = next;
		if (startLocation.line == 0)
			startLocation = next.GetStartLocation();
		lexer.NextCompoundToken();

		// 4. Name (optional: anonymous unions `union { ... };` are allowed, e.g. for swizzles)
		Token nameToken = lexer.PeekCompound();
		if (nameToken.GetType() == TokenType::Identifier)
		{
			m_NameToken = nameToken;
			lexer.NextCompoundToken();
		}

		// 5. Opening brace
		Token openBrace = lexer.PeekCompound();
		if (openBrace.GetType() == TokenType::OpenCurl)
		{
			m_OpenBraceToken = openBrace;
			m_HasOpenBrace = true;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				openBrace.GetStartLocation(),
				Core::StringFormat::Format("Expected an opening brace for the union body, got '{0}'", openBrace.GetText()),
				ErrorSeverity::Error);
			return;
		}

	// 6. Parse members: plain fields, anonymous-struct views (swizzles), and nested named unions
	while (true)
	{
		Token t = lexer.PeekCompound();

		if (t.GetType() == TokenType::CloseCurl || t.GetType() == TokenType::EndOfFile)
			break;

		// A lane view first: inside a union `packed T[N] data;` names the region's lanes, it is not a field.
		Ref<AstNode> member = SwizzleViewNode::TryParse(state); // packed/unpacked lane view: `packed T x, y;`
		if (!member)
			member = VariableDefinitionNode::TryParse(state);
		if (!member)
			member = ObjectDefinitionNode::TryParse(state); // anonymous struct view: `struct { float x, y; }`
		if (!member)
			member = UnionDefinitionNode::TryParse(state);  // nested named union
		if (member)
		{
			m_Members.Push(member);
			continue;
		}

		errorTracker.AddError(
			t.GetStartLocation(),
			Core::StringFormat::Format("Unexpected token '{0}' in union body", t.GetText()),
			ErrorSeverity::Error);
		lexer.NextCompoundToken();
	}

		// 7. Closing brace
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
				Core::StringFormat::Format("Expected '}}' to close union, got '{0}'", closeBrace.GetText()),
				ErrorSeverity::Error);
		}

		// 8. Optional trailing semicolon
		Token semi = lexer.PeekCompound();
		if (semi.GetType() == TokenType::SemiColon)
		{
			m_SemicolonToken = semi;
			endLocation = semi.GetEndLocation();
			lexer.NextCompoundToken();
		}

		if (endLocation.line == 0)
			endLocation = startLocation;

		// Build children
		m_Children.Clear();
		for (uint32 i = 0; i < m_Annotations.Count(); ++i)
			m_Children.Push(m_Annotations[i]);
		for (uint32 i = 0; i < m_Members.Count(); ++i)
			m_Children.Push(m_Members[i]);
	}

} // namespace Scripting
