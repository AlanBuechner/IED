#include "NamespaceQualifiedNameNode.h"
#include "Parser/PrattParser.h"
#include "QualTypeNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	// --- TemplateArgumentListNode ---

	Core::String TemplateArgumentListNode::ToString() const
	{
		Core::String result = "<";
		for (uint32 i = 0; i < m_Arguments.Count(); ++i)
		{
			if (i > 0)
				result += ", ";
			result += m_Arguments[i]->ToString();
		}
		result += ">";
		return result;
	}

	// --- NamespaceQualifiedNameNode ---

	const Token& NamespaceQualifiedNameNode::GetFirstIdentifierToken() const
	{
		return m_Segments[0].nameToken;
	}

	static Ref<AstNode> ParseTemplateArgument(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		Token next = lexer.PeekCompound();

		// Inside template args we know something must be parsed. Peek to decide
		// what kind, then parse directly without TryParse/undo speculation.
		// This avoids nested LexerUndoScope commits that corrupt outer positions.
		if (next.GetType() == TokenType::Identifier || next.GetType() == TokenType::Const)
		{
			auto qualType = CreateRef<QualTypeNode>();
			qualType->ParseBody(state);
			return PrattParser::ExpandIfPack(state, qualType);
		}

		// Otherwise a constant expression (`3`, `-1`, `(N * 2)`, `sizeof(T)`); a bare name is parsed as a type
		// above and read as a constant when its parameter takes a value.
		return PrattParser::ExpandIfPack(state, PrattParser::ParseTemplateArgumentExpression(state));
	}

	Ref<TemplateArgumentListNode> NamespaceQualifiedNameNode::ParseTemplateArguments(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		auto argsNode = CreateRef<TemplateArgumentListNode>();

		Token openAngle = lexer.PeekCompound();
		argsNode->startLocation = openAngle.GetStartLocation();
		argsNode->m_OpenAngleToken = openAngle;
		lexer.NextCompoundToken(); // consume '<'
		lexer.EnterTemplateContext(); // track template depth for >> handling

		// `Buf<>`: no arguments written (every parameter takes its default).
		if (lexer.PeekCompound().GetType() == TokenType::Greater)
		{
			argsNode->m_CloseAngleToken = lexer.PeekCompound();
			argsNode->endLocation = argsNode->m_CloseAngleToken.GetEndLocation();
			lexer.NextCompoundToken();
			lexer.LeaveTemplateContext();
			return argsNode;
		}

		// Parse first argument
		auto arg = ParseTemplateArgument(state);
		if (arg != nullptr)
		{
			argsNode->m_Arguments.Push(arg);
		}
		else
		{
			Token bad = lexer.PeekCompound();
			errorTracker.AddError(
				bad.GetStartLocation(),
				Core::StringFormat::Format("Expected template argument, got '{0}'", bad.GetText()),
				ErrorSeverity::Error);

			// Recovery: skip until '>' or sync point
			while (true)
			{
				Token t = lexer.PeekCompound();
				if (t.GetType() == TokenType::Greater ||
					t.GetType() == TokenType::EndOfFile ||
					t.GetType() == TokenType::SemiColon)
					break;
				lexer.NextCompoundToken();
			}
		}

		// Parse remaining comma-separated arguments
		while (true)
		{
			Token next = lexer.PeekCompound();
			if (next.GetType() != TokenType::Comma)
				break;

			lexer.NextCompoundToken(); // consume ','

			arg = ParseTemplateArgument(state);
			if (arg != nullptr)
			{
				argsNode->m_Arguments.Push(arg);
			}
			else
			{
				Token bad = lexer.PeekCompound();
				errorTracker.AddError(
					bad.GetStartLocation(),
					Core::StringFormat::Format("Expected template argument, got '{0}'", bad.GetText()),
					ErrorSeverity::Error);

				// Recovery: skip until '>' or sync point
				while (true)
				{
					Token t = lexer.PeekCompound();
					if (t.GetType() == TokenType::Greater ||
						t.GetType() == TokenType::EndOfFile ||
						t.GetType() == TokenType::SemiColon)
						break;
					lexer.NextCompoundToken();
				}
			}
		}

		Token closeAngle = lexer.PeekCompound();
		if (closeAngle.GetType() == TokenType::Greater)
		{
			argsNode->m_CloseAngleToken = closeAngle;
			argsNode->endLocation = closeAngle.GetEndLocation();
			lexer.NextCompoundToken(); // consume '>'
			lexer.LeaveTemplateContext();
		}
		else
		{
			errorTracker.AddError(
				closeAngle.GetStartLocation(),
				Core::StringFormat::Format("Expected '>' to close template arguments, got '{0}'", closeAngle.GetText()),
				ErrorSeverity::Error);
			argsNode->m_CloseAngleToken = closeAngle; // store what we found for error reporting
			argsNode->endLocation = closeAngle.GetEndLocation();
			lexer.LeaveTemplateContext(); // still decrement on error recovery
		}

		return argsNode;
	}

	void NamespaceQualifiedNameNode::ParseSegment(ParserState& state, Segment& outSegment)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		Token name = lexer.PeekCompound();
		if (name.GetType() != TokenType::Identifier && name.GetType() != TokenType::FunPtr)
		{
			errorTracker.AddError(
				name.GetStartLocation(),
				Core::StringFormat::Format("Expected identifier for name segment, got '{0}'", name.GetText()),
				ErrorSeverity::Error);
			outSegment.nameToken = name; // store the bad token for error reporting
			return;
		}

		outSegment.nameToken = name;
		lexer.NextCompoundToken(); // consume identifier

		// Check for optional template arguments: '<' ... '>'
		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Less)
		{
			outSegment.templateArgs = ParseTemplateArguments(state);
		}
	}

	Ref<NamespaceQualifiedNameNode> NamespaceQualifiedNameNode::FromParts(const Token& nameToken, const Ref<TemplateArgumentListNode>& tmplArgs)
	{
		auto node = CreateRef<NamespaceQualifiedNameNode>();
		Segment seg;
		seg.nameToken = nameToken;
		seg.templateArgs = tmplArgs;
	node->m_Segments.Push(seg);
	node->startLocation = nameToken.GetStartLocation();
	node->endLocation = (tmplArgs ? tmplArgs->endLocation : nameToken.GetEndLocation());
	return node;
}

Ref<NamespaceQualifiedNameNode> NamespaceQualifiedNameNode::FromSegments(const Core::Vector<Segment>& segments)
{
	auto node = CreateRef<NamespaceQualifiedNameNode>();
	for (const auto& s : segments)
		node->m_Segments.Push(s);
	if (!node->m_Segments.Empty())
	{
		const auto& last = node->m_Segments.Back();
		node->startLocation = node->m_Segments[0].nameToken.GetStartLocation();
		node->endLocation = (last.templateArgs ? last.templateArgs->endLocation : last.nameToken.GetEndLocation());
	}
	return node;
}

Ref<TemplateArgumentListNode> TemplateArgumentListNode::FromArguments(const Token& openAngle, const Core::Vector<Ref<AstNode>>& args, const Token& closeAngle)
{
	auto node = CreateRef<TemplateArgumentListNode>();
	node->m_OpenAngleToken = openAngle;
	for (const auto& a : args)
		node->m_Arguments.Push(a);
	node->m_CloseAngleToken = closeAngle;
	if (!args.Empty())
	{
		node->startLocation = args[0]->startLocation;
		node->endLocation = args.Back()->endLocation;
	}
	else
	{
		node->startLocation = openAngle.GetStartLocation();
		node->endLocation = closeAngle.GetEndLocation();
	}
	return node;
}

	Ref<NamespaceQualifiedNameNode> NamespaceQualifiedNameNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token first = lexer.PeekCompound();
		if (first.GetType() != TokenType::Identifier && first.GetType() != TokenType::FunPtr)
			return nullptr;

		// Speculative parse: try to parse at least one segment.
		// Errors are NOT suppressed so that partial parses with errors still report them.
		auto undo = LexerUndoScope(lexer);

		auto node = CreateRef<NamespaceQualifiedNameNode>();
		node->ParseBody(state);

		// Valid only if we have at least one segment with a valid identifier
		if (node->m_Segments.Empty())
			return nullptr;

		if (node->m_Segments[0].nameToken.GetType() != TokenType::Identifier &&
			node->m_Segments[0].nameToken.GetType() != TokenType::FunPtr)
			return nullptr;

		undo.Commit();
		return node;
	}

	void NamespaceQualifiedNameNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_Segments.Clear();

		// Parse first segment
		Segment firstSegment;
		ParseSegment(state, firstSegment);
		if (firstSegment.nameToken.GetType() != TokenType::Identifier)
			return; // invalid first segment - nothing to parse

		startLocation = firstSegment.nameToken.GetStartLocation();
		m_Segments.Push(firstSegment);

		// Parse remaining segments: '::' identifier [template_args]
		while (true)
		{
			Token next = lexer.PeekCompound();
			if (next.GetType() != TokenType::DoubleColon)
				break;

			lexer.NextCompoundToken(); // consume '::'

			Segment segment;
			ParseSegment(state, segment);

			if (segment.nameToken.GetType() == TokenType::Identifier)
			{
				m_Segments.Push(segment);
			}
			else
			{
				// Missing identifier after '::' - error already recorded in ParseSegment
				// Return partial node with what we have
				break;
			}
		}

		if (!m_Segments.Empty())
		{
			const auto& last = m_Segments.Back();
			if (last.templateArgs != nullptr)
				endLocation = last.templateArgs->endLocation;
			else
				endLocation = last.nameToken.GetEndLocation();
		}
	}

	Core::String NamespaceQualifiedNameNode::ToString() const
	{
		Core::String result;
		for (uint32 i = 0; i < m_Segments.Count(); ++i)
		{
			if (i > 0)
				result += "::";
			const auto& seg = m_Segments[i];
			result += seg.nameToken.GetText();
			if (seg.templateArgs != nullptr)
				result += seg.templateArgs->ToString();
		}
		return result;
	}

} // namespace Scripting
