#include "AnnotationNode.h"
#include "../Expressions/LiteralNode.h"
#include "Parser/NearNames.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"
#include <algorithm>
#include <string>
#include <vector>

namespace Scripting
{

	// --- AnnotationItem ---

	Core::String AnnotationItem::ToString() const
	{
		if (m_Arguments.Empty())
			return m_Name;
		return Core::StringFormat::Format("{0}(args)", m_Name);
	}

	namespace
	{
		const char* const kCompilerAnnotations[] = { "operator", "test", "test_setup", "test_teardown", "group", "link", "uuid", "target",
			kAnnotationDeclaration };
	}

	std::span<const char* const> CompilerAnnotationNames() { return kCompilerAnnotations; }

	bool IsCompilerAnnotationName(const Core::String& name)
	{
		for (const char* compiler : kCompilerAnnotations)
			if (name == compiler) return true;
		return false;
	}

	bool AnnotationItem::IsCompilerAnnotation() const { return IsCompilerAnnotationName(m_Name); }

	// D-42: an unknown annotation one edit from a short compiler annotation, or two from a longer one, is
	// probably a typo of it. Null when it is not close to any.
	const char* AnnotationItem::NearCompilerAnnotation() const
	{
		if (IsCompilerAnnotation()) return nullptr;
		for (const char* name : kCompilerAnnotations)
			if (IsNearMiss(m_Name.c_str(), name)) return name;
		return nullptr;
	}

	Core::String AnnotationItem::GetOwnName() const
	{
		return m_NameTokens.Empty() ? m_Name : m_NameTokens.Back().GetText();
	}

	// --- AnnotationNode ---

	uint32 AnnotationNode::TotalAnnotationCount() const
	{
		uint32 total = 0;
		for (const auto& group : m_BracketGroups)
			total += group.annotations.Count();
		return total;
	}

	std::span<const Ref<AstNode>> AnnotationNode::Children() const
	{
		return m_FlattenedChildren;
	}

	static Core::String QualifiedNameToString(const Core::Vector<Token>& tokens)
	{
		Core::String result;
		for (uint32 i = 0; i < tokens.Count(); ++i)
		{
			if (i > 0)
				result += "::";
			result += tokens[i].GetText();
		}
		return result;
	}

	static bool TryParseQualifiedName(ParserState& state, Core::Vector<Token>& outTokens)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		Token first = lexer.PeekCompound();
		if (first.GetType() != TokenType::Identifier)
			return false;

		outTokens.Clear();
		outTokens.Push(first);
		lexer.NextCompoundToken();

		while (true)
		{
			Token next = lexer.PeekCompound();
			if (next.GetType() != TokenType::DoubleColon)
				break;

			lexer.NextCompoundToken(); // consume '::' as one compound token

			Token identifier = lexer.PeekCompound();
			if (identifier.GetType() != TokenType::Identifier)
			{
				errorTracker.AddError(
					identifier.GetStartLocation(),
					Core::StringFormat::Format("Expected identifier after '::', got '{0}'", identifier.GetText()),
					ErrorSeverity::Error);
				break;
			}

			outTokens.Push(identifier);
			lexer.NextCompoundToken();
		}

		return true;
	}

	TokenType AnnotationNode::PeekPastAnnotations(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		auto peek = LexerUndoScope(lexer);
		while (lexer.PeekCompound().GetType() == TokenType::OpenBracket)
		{
			auto annotation = CreateRef<AnnotationNode>();
			annotation->ParseBody(state);
		}
		const TokenType next = lexer.PeekCompound().GetType();
		if (next != TokenType::Public && next != TokenType::Private && next != TokenType::Protected) return next;
		lexer.NextCompoundToken();
		return lexer.PeekCompound().GetType();
	}

	Ref<AnnotationNode> AnnotationNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		if (token.GetType() != TokenType::OpenBracket)
			return nullptr;

		// Speculative parse: try to parse at least one bracket group with at least one valid annotation
		auto undo = LexerUndoScope(lexer);

		auto node = CreateRef<AnnotationNode>();

		// Collect all consecutive bracket groups into one node (standalone use)
		while (true)
		{
			Token t = lexer.PeekCompound();
			if (t.GetType() != TokenType::OpenBracket)
				break;
			node->ParseSingleBracketGroup(state);
		}

		// Valid only if we have at least one bracket group containing at least one annotation
		for (const auto& group : node->m_BracketGroups)
		{
			if (!group.annotations.Empty())
			{
				undo.Commit();
				return node;
			}
		}

		return nullptr; // all groups empty - not a valid annotation
	}

	void AnnotationNode::ParseAnnotation(ParserState& state, BracketGroup& group)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		auto item = CreateRef<AnnotationItem>();

		// Parse qualified name - required for a valid annotation
		if (!TryParseQualifiedName(state, item->m_NameTokens))
		{
			Token t = lexer.PeekCompound();
			errorTracker.AddError(
				t.GetStartLocation(),
				Core::StringFormat::Format("Expected annotation name (identifier), got '{0}'", t.GetText()),
				ErrorSeverity::Error);
			// Don't push invalid item - caller decides whether to continue or abort
			return;
		}

		item->m_Name = QualifiedNameToString(item->m_NameTokens);
		item->startLocation = item->m_NameTokens[0].GetStartLocation();

		Token lastToken = item->m_NameTokens.Back();

		// Optional arguments: '(' expression (',' expression)* ')'
		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::OpenParen)
		{
			lexer.NextCompoundToken(); // consume '('
			ParseArguments(state, *item);
			next = lexer.PeekCompound();
			if (next.GetType() == TokenType::CloseParen)
			{
				lastToken = next;
				lexer.NextCompoundToken(); // consume ')'
			}
			else
			{
				errorTracker.AddError(
					next.GetStartLocation(),
					Core::StringFormat::Format("Expected ')' after annotation arguments, got '{0}'", next.GetText()),
					ErrorSeverity::Error);
			}
			if (item->IsCompilerAnnotation()) CheckCompilerArguments(state, *item);
		}
		if (const char* meant = item->NearCompilerAnnotation())
			errorTracker.AddError(item->startLocation, Core::StringFormat::Format("unknown annotation '{0}': did you mean '{1}'? (an unknown "
				"annotation is kept for reflection, the compiler does not act on it)", item->GetName(), meant), ErrorSeverity::Warning);

		item->endLocation = lastToken.GetEndLocation();
		group.annotations.Push(item);
	}

	namespace
	{
		bool EndsArgument(TokenType type) { return type == TokenType::Comma || type == TokenType::CloseParen; }

		// The argument's tokens as a type that ends the argument, read ahead and rolled back; null when they do not read as one.
		Ref<QualTypeNode> ArgumentAsType(ParserState& state)
		{
			Lexer& lexer = state.GetLexer();
			auto undo = LexerUndoScope(lexer);
			state.GetErrorTracker().SuppressErrors();
			Ref<QualTypeNode> type = QualTypeNode::TryParse(state);
			const bool fits = type && !state.GetErrorTracker().SpeculationFailed() && EndsArgument(lexer.PeekCompound().GetType());
			state.GetErrorTracker().DiscardSuppressedErrors();
			undo.Cancel();
			return fits ? type : nullptr;
		}

		// The argument as an expression that ends it, consumed; null, with nothing consumed, when it does not read as one.
		Ref<AstNode> ArgumentAsExpression(ParserState& state)
		{
			Lexer& lexer = state.GetLexer();
			auto undo = LexerUndoScope(lexer);
			state.GetErrorTracker().SuppressErrors();
			Ref<AstNode> expression = PrattParser::ParseExpression(state);
			if (!expression || state.GetErrorTracker().SpeculationFailed() || !EndsArgument(lexer.PeekCompound().GetType()))
			{
				state.GetErrorTracker().DiscardSuppressedErrors();
				undo.Cancel();
				return nullptr;
			}
			state.GetErrorTracker().CommitSuppressedErrors();
			undo.Commit();
			return expression;
		}
	}

	void AnnotationNode::ParseArguments(ParserState& state, AnnotationItem& item)
	{
		Lexer& lexer = state.GetLexer();
		if (lexer.PeekCompound().GetType() == TokenType::CloseParen) return; // `[Name()]`: no arguments
		while (true)
		{
			Token argStart = lexer.PeekCompound();
			if (!ParseArgument(state, item))
			{
				state.GetErrorTracker().AddError(argStart.GetStartLocation(),
					Core::StringFormat::Format("Expected annotation argument expression, got '{0}'", argStart.GetText()), ErrorSeverity::Error);
				return;
			}
			if (lexer.PeekCompound().GetType() != TokenType::Comma) return;
			lexer.NextCompoundToken(); // consume ','
		}
	}

	// An expression, kept with the same tokens read as a type when they read as one (a name is a constant or a type: the
	// model decides); a type alone when only that reads (`int32*`); else the expression as far as it parses, its errors said.
	bool AnnotationNode::ParseArgument(ParserState& state, AnnotationItem& item)
	{
		if (state.GetLexer().PeekCompound().GetType() == TokenType::CloseParen) return false;
		Ref<QualTypeNode> type = ArgumentAsType(state);
		Ref<AstNode> argument = ArgumentAsExpression(state);
		if (!argument && type)
		{
			type = QualTypeNode::TryParse(state);
			argument = type;
		}
		if (!argument) argument = PrattParser::ParseExpression(state);
		if (!argument) return false;
		item.m_Arguments.Push(argument);
		item.m_ArgumentTypes.Push(type);
		return true;
	}

	void AnnotationNode::CheckCompilerArguments(ParserState& state, const AnnotationItem& item)
	{
		for (const auto& argument : item.GetArguments())
		{
			const auto* literal = dynamic_cast<const LiteralNode*>(argument.Get());
			if (literal && literal->GetKind() == LiteralNode::LiteralKind::String) continue;
			state.GetErrorTracker().AddError(argument->startLocation,
				Core::StringFormat::Format("'{0}' takes string literal arguments", item.GetName()), ErrorSeverity::Error);
		}
	}

	void AnnotationNode::ParseSingleBracketGroup(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		Token openBracket = lexer.PeekCompound();
		if (openBracket.GetType() != TokenType::OpenBracket)
			return;

		lexer.NextCompoundToken(); // consume '['

		BracketGroup group;
		group.openBracketToken = openBracket;

		ParseAnnotation(state, group);

		while (true)
		{
			Token next = lexer.PeekCompound();
			if (next.GetType() != TokenType::Comma)
				break;

			lexer.NextCompoundToken(); // consume ','
			ParseAnnotation(state, group);
		}

		{
			Token closeBracket = lexer.PeekCompound();
			if (closeBracket.GetType() == TokenType::CloseBracket)
			{
				group.closeBracketToken = closeBracket;
				lexer.NextCompoundToken(); // consume ']'
			}
			else
			{
				errorTracker.AddError(
					closeBracket.GetStartLocation(),
					Core::StringFormat::Format("Expected ']' to close annotation group, got '{0}'", closeBracket.GetText()),
					ErrorSeverity::Error);
			}
		}

		m_BracketGroups.Push(group);

		for (auto& item : group.annotations)
			m_FlattenedChildren.Push(item);

		if (m_BracketGroups.Count() == 1)
			startLocation = m_BracketGroups[0].openBracketToken.GetStartLocation();
		endLocation = m_BracketGroups.Back().closeBracketToken.GetEndLocation();
	}

	void AnnotationNode::ParseBody(ParserState& state)
	{
		m_BracketGroups.Clear();
		m_FlattenedChildren.Clear();
		ParseSingleBracketGroup(state);
	}

	Core::String AnnotationNode::ToString() const
	{
		if (m_BracketGroups.Empty())
			return "[empty]";

		Core::String result;
		for (uint32 i = 0; i < m_BracketGroups.Count(); ++i)
		{
			const auto& group = m_BracketGroups[i];
			result += "[";
			for (uint32 j = 0; j < group.annotations.Count(); ++j)
			{
				if (j > 0)
					result += ", ";
				result += group.annotations[j]->ToString();
			}
			result += "]";
		}
		return result;
	}

} // namespace Scripting
