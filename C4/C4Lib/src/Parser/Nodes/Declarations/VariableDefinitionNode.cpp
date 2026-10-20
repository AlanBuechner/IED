#include "VariableDefinitionNode.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"
#include "DeclarationShapes.h"
#include "ObjectDefinitionNode.h"
#include "UnionDefinitionNode.h"
#include "../Expressions/LiteralNode.h"
#include "../Expressions/IdentifierExpressionNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	static bool IsModifierKeyword(TokenType type, const Core::String& text)
	{
		return type == TokenType::Static || type == TokenType::Const ||
		       (type == TokenType::Identifier && text == "volatile") || text == "thread_local";
	}

	static bool IsAccessModifier(TokenType type)
	{
		return type == TokenType::Public || type == TokenType::Private || type == TokenType::Protected;
	}

	std::span<const Ref<AstNode>> VariableDefinitionNode::Children() const
	{
		return m_Children;
	}

	Core::String VariableDefinitionNode::ToString() const
	{
		Core::String result;

		if (m_IsStatic)
			result += "static ";

		switch (m_AccessModifier) {
			case AccessModifier::Public: result += "public "; break;
			case AccessModifier::Private: result += "private "; break;
			case AccessModifier::Protected: result += "protected "; break;
			default: break;
		}

		if (m_IsConst)
			result += "const ";

		if (m_Type)
			result += m_Type->ToString();

		if (!m_NameToken.GetText().empty())
		{
			result += " ";
			result += m_NameToken.GetText();
		}

		return result;
	}

	Ref<VariableDefinitionNode> VariableDefinitionNode::TryParse(ParserState& state, bool consumeSemicolon)
	{
		Lexer& lexer = state.GetLexer();

		Token first = lexer.PeekCompound();

		// Must start with '[' (annotations), access modifier, or something that could be a type/modifier
		if (first.GetType() != TokenType::OpenBracket &&
			first.GetType() != TokenType::Const &&
			first.GetType() != TokenType::Identifier &&
			!IsAccessModifier(first.GetType()) &&
			first.GetType() != TokenType::Static &&
			first.GetType() != TokenType::FunPtr)
		{
			return nullptr;
		}

	auto undo = LexerUndoScope(lexer);
	state.GetErrorTracker().SuppressErrors();

	auto node = CreateRef<VariableDefinitionNode>();
	node->ParseBody(state, consumeSemicolon);

	if (!node->m_Type || (!node->m_Type->BaseName() && !node->m_Type->FuncPtrType()) ||
		(node->m_Type->BaseName() && node->m_Type->BaseName()->GetSegments().Empty()))
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	if (node->m_NameToken.GetText().empty())
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}
	// Validate: after name, must have ';' or '=' (initializer) to be a variable definition.
	// Comma declarator lists (`int a = 1, b;`) are valid even when the last declarator has no '='.
	// A direct-construction declarator (`T v(args);`, inside a body) is also a variable definition -
	// its ctor args were consumed, so accept it on that basis too.
	bool anyCtorArgs = false;
	for (const auto& dd : node->m_Declarators)
		if (!dd.m_CtorArgs.Empty()) { anyCtorArgs = true; break; }
	if (consumeSemicolon &&
		node->m_SemicolonToken.GetType() != TokenType::SemiColon &&
		node->m_AssignmentToken.GetType() != TokenType::Equal &&
		!anyCtorArgs &&
		node->DeclaratorCount() <= 1)
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	state.GetErrorTracker().CommitSuppressedErrors();
	undo.Commit();
	return node;
	}

	void VariableDefinitionNode::ParseBody(ParserState& state, bool consumeSemicolon)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_Annotations.Clear();
		m_AccessModifier = AccessModifier::None;
		m_IsStatic = false;
		m_IsConst = false;
		m_IsVolatile = false;
		m_IsThreadLocal = false;
		m_Type = nullptr;
		m_NameToken = Token();
		m_Initializer = nullptr;

		// 1. Loop: parse annotations while next token is '['
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

		// 2. Check for access modifier
		Token next = lexer.PeekCompound();
		if (IsAccessModifier(next.GetType()))
		{
			m_AccessModifier = AccessModifierOf(next.GetType());
			m_AccessModifierToken = next;
			if (startLocation.line == 0)
				startLocation = next.GetStartLocation();
			lexer.NextCompoundToken();
			next = lexer.PeekCompound();
		}

		// 3. Loop for modifiers: static, const, volatile, thread_local
		while (true)
		{
			Token mod = lexer.PeekCompound();
			bool foundModifier = false;

			if (mod.GetType() == TokenType::Static)
			{
				m_IsStatic = true;
				m_StaticToken = mod;
				foundModifier = true;
			}
			else if (mod.GetType() == TokenType::Const)
			{
				m_IsConst = true;
				m_ConstToken = mod;
				foundModifier = true;
			}
			else if (mod.GetType() == TokenType::Identifier && mod.GetText() == "volatile")
			{
				m_IsVolatile = true;
				m_VolatileToken = mod;
				foundModifier = true;
			}
			else if (mod.GetType() == TokenType::Identifier && mod.GetText() == "thread_local")
			{
				m_IsThreadLocal = true;
				m_ThreadLocalToken = mod;
				foundModifier = true;
			}

			if (!foundModifier)
				break;

			if (startLocation.line == 0)
				startLocation = mod.GetStartLocation();
			lexer.NextCompoundToken();
		}

		// 4. Parse type - required
		next = lexer.PeekCompound();
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
				Core::StringFormat::Format("Expected variable type, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}

		// The modifier loop above consumed a leading `const` before the type node parsed: hand it back, so
		// the type says what is const - the value, or the referent of `const T*` / `const T&` (D-30).
		if (m_IsConst)
			m_Type->SetLeadingConstFrom(m_ConstToken);
		}
		else
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected variable type, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}

	// 5. Declarator list: identifier ('=' expression)? (',' identifier ('=' expression)?) *
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
			Core::StringFormat::Format("Expected identifier for variable name, got '{0}'", nameToken.GetText()),
			ErrorSeverity::Error);
		return;
	}

	m_Declarators.Clear();
	auto parseDeclaratorInit = [&](Declarator& d)
	{
		Token afterName = lexer.PeekCompound();
		if (afterName.GetType() == TokenType::Equal)
		{
			m_AssignmentToken = afterName;
			lexer.NextCompoundToken();

			d.m_Initializer = PrattParser::ParseExpression(state);
			if (!d.m_Initializer)
			{
				Token bad = lexer.PeekCompound();
				errorTracker.AddError(
					bad.GetStartLocation(),
					Core::StringFormat::Format("Expected initializer expression, got '{0}'", bad.GetText()),
					ErrorSeverity::Error);
			}
		}
		else if (afterName.GetType() == TokenType::OpenParen)
		{
			// Direct construction: `T v(args);`. `T f(...) { ... }` is a function, and outside a body so is a
			// group that can be a parameter list (a prototype); both bail here so FunctionDefinitionNode claims them.
			if (!ParenGroupIsFollowedByBody(lexer) && (state.GetBlockDepth() > 0 || !ParenGroupIsParameterList(state)))
			{
				lexer.NextCompoundToken(); // consume '('
				Token close = lexer.PeekCompound();
				if (close.GetType() == TokenType::CloseParen)
					lexer.NextCompoundToken(); // empty arg list: `T v();`
				else
				{
					for (;;)
					{
						d.m_CtorArgs.Push(PrattParser::ParseArgument(state)); // `T v(args...)` expands a pack
						Token sep = lexer.PeekCompound();
						if (sep.GetType() == TokenType::Comma) { lexer.NextCompoundToken(); continue; }
						break;
					}
					close = lexer.PeekCompound();
					if (close.GetType() != TokenType::CloseParen)
						errorTracker.AddError(close.GetStartLocation(),
							Core::StringFormat::Format("Expected ')' after constructor arguments, got '{0}'", close.GetText()),
							ErrorSeverity::Error);
					else
						lexer.NextCompoundToken(); // consume ')'
				}
			}
		}
	};

	{
		Declarator d;
		d.m_NameToken = m_NameToken;
		parseDeclaratorInit(d);
		m_Declarators.Push(d);
	}

	while (lexer.PeekCompound().GetType() == TokenType::Comma)
	{
		lexer.NextCompoundToken();
		Token n2 = lexer.PeekCompound();
		if (n2.GetType() != TokenType::Identifier)
		{
			errorTracker.AddError(
				n2.GetStartLocation(),
				Core::StringFormat::Format("Expected identifier in variable declarator list, got '{0}'", n2.GetText()),
				ErrorSeverity::Error);
			break;
		}
		lexer.NextCompoundToken();
		Declarator d;
		d.m_NameToken = n2;
		parseDeclaratorInit(d);
		m_Declarators.Push(d);
	}

	// Legacy accessor mirrors the first declarator's initializer.
	m_Initializer = m_Declarators[0].m_Initializer;

		// 7. Expect semicolon (unless the caller manages its own terminator, e.g. a for-init clause)
		Token semi = lexer.PeekCompound();
		if (!consumeSemicolon)
		{
			// Leave the terminating ';' for the enclosing construct to consume.
			if (m_Initializer)
				endLocation = m_Initializer->endLocation;
			else if (!m_NameToken.GetText().empty())
				endLocation = m_NameToken.GetEndLocation();
			else if (m_Type)
				endLocation = m_Type->endLocation;

			m_Children.Clear();
			for (uint32 i = 0; i < m_Annotations.Count(); ++i)
				m_Children.Push(m_Annotations[i]);
			if (m_Type)
				m_Children.Push(m_Type);
			if (m_Initializer)
				m_Children.Push(m_Initializer);
			return;
		}

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
				Core::StringFormat::Format("Expected ';' after variable declaration, got '{0}'", semi.GetText()),
				ErrorSeverity::Error);
			endLocation = startLocation;
		}

		if (endLocation.line == 0)
		{
			if (m_Initializer)
				endLocation = m_Initializer->endLocation;
			else if (!m_NameToken.GetText().empty())
				endLocation = m_NameToken.GetEndLocation();
			else if (m_Type)
				endLocation = m_Type->endLocation;
		}

		m_Children.Clear();
		for (uint32 i = 0; i < m_Annotations.Count(); ++i)
			m_Children.Push(m_Annotations[i]);
		if (m_Type)
			m_Children.Push(m_Type);
		if (m_Initializer)
			m_Children.Push(m_Initializer);
	}

} // namespace Scripting
