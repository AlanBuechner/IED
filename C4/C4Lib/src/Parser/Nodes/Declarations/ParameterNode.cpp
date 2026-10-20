#include "ParameterNode.h"
#include "Lexer/Lexer.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> ParameterNode::Children() const
	{
		return m_Children;
	}

	Ref<ParameterNode> ParameterNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token first = lexer.PeekCompound();

		// Must start with '[' (annotations) or identifier/const/delegate (type start)
		if (first.GetType() != TokenType::OpenBracket &&
			first.GetType() != TokenType::Const &&
			first.GetType() != TokenType::Identifier &&
			first.GetType() != TokenType::FunPtr)
		{
			return nullptr;
		}

		auto undo = LexerUndoScope(lexer);
		state.GetErrorTracker().SuppressErrors();

		auto node = CreateRef<ParameterNode>();
		node->ParseBody(state);

	if (!node->m_Type || (!node->m_Type->BaseName() && !node->m_Type->FuncPtrType()) ||
		(node->m_Type->BaseName() && node->m_Type->BaseName()->GetSegments().Empty()))
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return node;
	}

	void ParameterNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_Annotations.Clear();
		m_Type = nullptr;
		m_IsPack = false;
		m_NameToken = Token();
		m_DefaultValue = nullptr;

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
				errorTracker.AddError(
					t.GetStartLocation(),
					Core::StringFormat::Format("Expected valid annotation, got '{0}'", t.GetText()),
					ErrorSeverity::Error);
				break;
			}
		}

		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Identifier || next.GetType() == TokenType::Const || next.GetType() == TokenType::FunPtr)
		{
			m_Type = CreateRef<QualTypeNode>();
			m_Type->ParseBody(state);

			if (startLocation.line == 0)
				startLocation = m_Type->startLocation;

		if (!m_Type->BaseName() && !m_Type->FuncPtrType())
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected parameter type, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}
		}
		else
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected parameter type, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}

		if (lexer.PeekCompound().GetType() == TokenType::Ellipsis)
		{
			m_IsPack = true;
			lexer.NextCompoundToken();
		}

		Token maybeName = lexer.PeekCompound();
		if (maybeName.GetType() == TokenType::Identifier)
		{
			m_NameToken = maybeName;
			lexer.NextCompoundToken();
		}

		Token afterName = lexer.PeekCompound();
		if (afterName.GetType() == TokenType::Equal)
		{
			lexer.NextCompoundToken();

			m_DefaultValue = PrattParser::ParseExpression(state); // stops at the `,` or `)` after it
			if (!m_DefaultValue)
			{
				Token bad = lexer.PeekCompound();
				errorTracker.AddError(
					bad.GetStartLocation(),
					Core::StringFormat::Format("Expected default value expression, got '{0}'", bad.GetText()),
					ErrorSeverity::Error);
			}
		}

		if (m_DefaultValue)
			endLocation = m_DefaultValue->endLocation;
		else if (HasName())
			endLocation = m_NameToken.GetEndLocation();
		else if (m_Type)
			endLocation = m_Type->endLocation;

		m_Children.Clear();
		for (uint32 i = 0; i < m_Annotations.Count(); ++i)
			m_Children.Push(m_Annotations[i]);
		if (m_Type)
			m_Children.Push(m_Type);
		if (m_DefaultValue)
			m_Children.Push(m_DefaultValue);
	}

	Core::String ParameterNode::ToString() const
	{
		Core::String result;

		for (uint32 i = 0; i < m_Annotations.Count(); ++i)
		{
			result += m_Annotations[i]->ToString();
		}

		if (!result.empty())
			result += " ";

		if (m_Type)
			result += m_Type->ToString();

		if (HasName())
		{
			result += " ";
			result += m_NameToken.GetText();
		}

		if (HasDefaultValue())
		{
			result += " = ";
			result += m_DefaultValue->ToString();
		}

		return result;
	}

} // namespace Scripting
