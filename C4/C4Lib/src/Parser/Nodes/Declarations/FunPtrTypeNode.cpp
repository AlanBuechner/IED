#include "FunPtrTypeNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	Core::String FunPtrTypeNode::ToString() const
	{
		Core::String result = m_IsFunc ? "func<" : "funcptr<";
		if (m_ReturnType)
			result += m_ReturnType->ToString();
		else
			result += "void";
		result += "(";
		for (uint32 i = 0; i < m_Parameters.Count(); ++i)
		{
			if (i > 0)
				result += ", ";
			if (m_Parameters[i])
				result += m_Parameters[i]->ToString();
		}
		result += ")>";
		return result;
	}

	std::span<const Ref<AstNode>> FunPtrTypeNode::Children() const
	{
		return m_Children;
	}

	bool FunPtrTypeNode::StartsSignatureType(const Lexer& lexer)
	{
		// `func` is a type former only before `<`, so it stays usable as an ordinary name.
		const Token first = lexer.PeekCompound();
		if (first.GetType() == TokenType::FunPtr) return true;
		return first.GetType() == TokenType::Identifier && first.GetText() == kFuncKeyword && lexer.PeekCompound(1).GetType() == TokenType::Less;
	}

	Ref<FunPtrTypeNode> FunPtrTypeNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		Token first = lexer.PeekCompound();

		if (!StartsSignatureType(lexer))
			return nullptr;

		auto undo = LexerUndoScope(lexer);
		state.GetErrorTracker().SuppressErrors();

		auto node = CreateRef<FunPtrTypeNode>();
		node->ParseBody(state);

	// Valid if we got the closing '>'
	if (node->endLocation.line != 0)
	{
		state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return node;
	}

	state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	void FunPtrTypeNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_ReturnType = nullptr;
		m_Parameters.Clear();

		// 1. funcptr keyword, or the contextual `func`
		Token funcPtrToken = lexer.PeekCompound();
		m_IsFunc = funcPtrToken.GetType() == TokenType::Identifier;
		if (!StartsSignatureType(lexer))
		{
			errorTracker.AddError(
				funcPtrToken.GetStartLocation(),
				Core::StringFormat::Format("Expected 'funcptr' keyword, got '{0}'", funcPtrToken.GetText()),
				ErrorSeverity::Error);
			return;
		}
		m_FunPtrKeywordToken = funcPtrToken;
		if (startLocation.line == 0)
			startLocation = funcPtrToken.GetStartLocation();
		lexer.NextCompoundToken();

		// 2. Expect '<'
		Token openAngle = lexer.PeekCompound();
		if (openAngle.GetType() != TokenType::Less)
		{
			errorTracker.AddError(
				openAngle.GetStartLocation(),
				Core::StringFormat::Format("Expected '<' after 'funcptr', got '{0}'", openAngle.GetText()),
				ErrorSeverity::Error);
			return;
		}
		m_OpenAngleToken = openAngle;
		lexer.NextCompoundToken();
		lexer.EnterTemplateContext(); // track template depth for >> handling

		// 3. Return type (can be funcptr<...> for nested function pointers)
		m_ReturnType = QualTypeNode::ParseReturnType(state); // `funcptr<[float, bool](int32)>` returns a list

		// 4. Open parenthesis for parameters
		Token openParen = lexer.PeekCompound();
		if (openParen.GetType() != TokenType::OpenParen)
		{
			errorTracker.AddError(
				openParen.GetStartLocation(),
				Core::StringFormat::Format("Expected '(' after return type in funcptr, got '{0}'", openParen.GetText()),
				ErrorSeverity::Error);
			lexer.LeaveTemplateContext();
			endLocation = startLocation;
			return;
		}
		m_OpenParenToken = openParen;
		lexer.NextCompoundToken();
		ParameterNode::SkipVoidList(lexer);

		// 5. Parse parameters
		while (true)
		{
			Token t = lexer.PeekCompound();
			if (t.GetType() == TokenType::CloseParen || t.GetType() == TokenType::EndOfFile)
				break;

			auto param = ParameterNode::TryParse(state);
			if (param && param->GetType())
			{
				m_Parameters.Push(param);
			}
			else
			{
				errorTracker.AddError(
					t.GetStartLocation(),
					Core::StringFormat::Format("Expected parameter in funcptr, got '{0}'", t.GetText()),
					ErrorSeverity::Error);
				break;
			}

			t = lexer.PeekCompound();
			if (t.GetType() == TokenType::Comma)
				lexer.NextCompoundToken();
			else if (t.GetType() != TokenType::CloseParen)
				break;
		}

		// 6. Close parenthesis
		Token closeParen = lexer.PeekCompound();
		if (closeParen.GetType() == TokenType::CloseParen)
		{
			m_CloseParenToken = closeParen;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				closeParen.GetStartLocation(),
				Core::StringFormat::Format("Expected ')' to close funcptr parameters, got '{0}'", closeParen.GetText()),
				ErrorSeverity::Error);
		}

		// 7. Expect '>'
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
				Core::StringFormat::Format("Expected '>' to close funcptr type, got '{0}'", closeAngle.GetText()),
				ErrorSeverity::Error);
		}

		lexer.LeaveTemplateContext(); // always leave template context

		if (endLocation.line == 0)
			endLocation = startLocation;

		m_Children.Clear();
		if (m_ReturnType)
			m_Children.Push(m_ReturnType);
		for (uint32 i = 0; i < m_Parameters.Count(); ++i)
			m_Children.Push(m_Parameters[i]);
	}


} // namespace Scripting
