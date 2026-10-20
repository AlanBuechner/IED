#include "TemplateDefinitionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{

	Ref<TemplateDefinitionNode> TemplateDefinitionNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token first = lexer.PeekCompound();

		if (first.GetType() != TokenType::Identifier && first.GetType() != TokenType::Template)
			return nullptr;

		if (first.GetType() == TokenType::Identifier && first.GetText() != "template")
			return nullptr;

		auto undo = LexerUndoScope(lexer);

		auto node = CreateRef<TemplateDefinitionNode>();
		node->ParseBody(state);

		if (node->m_OpenAngleToken.GetType() != TokenType::Less)
			return nullptr;

		undo.Commit();
		return node;
	}

	Ref<TemplateParameterNode> TemplateDefinitionNode::GetParameter(uint32 index) const
	{
		if (index >= m_Parameters.Count())
			return nullptr;
		return m_Parameters[index].As<TemplateParameterNode>();
	}

	void TemplateDefinitionNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		Token templateToken = lexer.PeekCompound();
		m_TemplateKeywordToken = templateToken;
		startLocation = templateToken.GetStartLocation();
		lexer.NextCompoundToken();

		Token openAngle = lexer.PeekCompound();
		if (openAngle.GetType() != TokenType::Less)
		{
			errorTracker.AddError(
				openAngle.GetStartLocation(),
				Core::StringFormat::Format("Expected '<' after 'template', got '{0}'", openAngle.GetText()),
				ErrorSeverity::Error);
			m_OpenAngleToken = openAngle;
			endLocation = openAngle.GetEndLocation();
			return;
		}

		m_OpenAngleToken = openAngle;
		lexer.NextCompoundToken();

		while (true)
		{
			Token next = lexer.PeekCompound();

			if (next.GetType() == TokenType::Greater || next.GetType() == TokenType::EndOfFile)
				break;

			auto param = TemplateParameterNode::TryParse(state);
			if (param != nullptr)
			{
				param->ParseBody(state);
				m_Parameters.Push(param);
			}
			else
			{
				errorTracker.AddError(
					next.GetStartLocation(),
					Core::StringFormat::Format("Expected template parameter, got '{0}'", next.GetText()),
					ErrorSeverity::Error);

				while (true)
				{
					Token t = lexer.PeekCompound();
					if (t.GetType() == TokenType::Greater ||
						t.GetType() == TokenType::Comma ||
						t.GetType() == TokenType::EndOfFile)
						break;
					lexer.NextCompoundToken();
				}
			}

			next = lexer.PeekCompound();
			if (next.GetType() != TokenType::Comma)
				break;

			lexer.NextCompoundToken();
		}

		Token closeAngle = lexer.PeekCompound();
		if (closeAngle.GetType() == TokenType::Greater)
		{
			m_CloseAngleToken = closeAngle;
			endLocation = closeAngle.GetEndLocation();
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				closeAngle.GetStartLocation(),
				Core::StringFormat::Format("Expected '>' to close template parameters, got '{0}'", closeAngle.GetText()),
				ErrorSeverity::Error);
			m_CloseAngleToken = closeAngle;
			endLocation = closeAngle.GetEndLocation();
		}
	}

	Core::String TemplateDefinitionNode::ToString() const
	{
		Core::String result = "template<";
		for (uint32 i = 0; i < m_Parameters.Count(); ++i)
		{
			if (i > 0)
				result += ", ";
			result += m_Parameters[i]->ToString();
		}
		result += ">";
		return result;
	}

	void TemplateDefinitionNode::BringTypeParametersInto(TypeParameterScope& scope) const
	{
		for (uint32 i = 0; i < ParameterCount(); ++i)
			if (Ref<TemplateParameterNode> parameter = GetParameter(i); parameter && parameter->Kind() == TemplateParameterKind::Type)
				scope.Add(parameter->GetName());
	}
} // namespace Scripting
