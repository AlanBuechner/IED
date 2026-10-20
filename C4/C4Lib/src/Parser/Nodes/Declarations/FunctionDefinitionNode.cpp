#include "FunctionDefinitionNode.h"
#include "DeclarationShapes.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"
#include "NamespaceQualifiedNameNode.h"
#include "TemplateDefinitionNode.h"
#include "ParameterNode.h"
#include "../Statements/CodeBlockNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	uint32 FunctionDefinitionNode::TrailingDefaultCount() const
	{
		uint32 count = 0;
		for (uint32 i = m_Parameters.Count(); i-- > 0 && m_Parameters[i]->GetDefaultValue();) ++count;
		return count;
	}

	static bool IsAccessModifier(TokenType type)
	{
		return type == TokenType::Public || type == TokenType::Private || type == TokenType::Protected;
	}

	std::vector<bool> FunctionDefinitionNode::ParameterConstness() const
	{
		std::vector<bool> constness;
		for (const auto& p : m_Parameters)
		{
			const auto* type = p ? dynamic_cast<const QualTypeNode*>(p->GetType().Get()) : nullptr;
			constness.push_back(type && type->DeclaresConstValue());
		}
		return constness;
	}

	std::span<const Ref<AstNode>> FunctionDefinitionNode::Children() const
	{
		return m_Children;
	}

	Core::String FunctionDefinitionNode::ToString() const
	{
		Core::String result;

		switch (m_AccessModifier) {
			case AccessModifier::Public: result += "public "; break;
			case AccessModifier::Private: result += "private "; break;
			case AccessModifier::Protected: result += "protected "; break;
			default: break;
		}

		if (m_IsStatic)
			result += "static ";
		if (m_IsVirtual)
			result += "virtual ";
		if (m_IsOverride)
			result += "override ";

		if (m_ReturnType)
			result += m_ReturnType->ToString();

		if (m_Name)
		{
			if (!result.empty())
				result += " ";
			result += m_Name->ToString();
		}

		result += "(";
		for (uint32 i = 0; i < m_Parameters.Count(); ++i)
		{
			if (i > 0)
				result += ", ";
			result += m_Parameters[i]->ToString();
		}
		if (m_IsVariadic)
		{
			if (!m_Parameters.Empty())
				result += ", ";
			result += "...";
		}
		result += ")";

		if (m_IsConstMethod) result += " const";
		if (m_IsNoexcept) result += " noexcept";
		if (m_IsPureVirtual) result += " = 0";

		if (m_ExtendsTypeName)
			result += " extends " + m_ExtendsTypeName->ToString();

		return result;
	}

	void FunctionDefinitionNode::CheckAbstract(ErrorTracker& errorTracker) const
	{
		if (!m_IsAbstract) return;
		const Core::String name = m_Name ? m_Name->ToString() : Core::String("?");
		if (m_Body)
			errorTracker.AddError(m_AbstractToken.GetStartLocation(), "abstract method '" + name + "' cannot have a body: derived types implement it",
				ErrorSeverity::Error);
		if (m_IsStatic)
			errorTracker.AddError(m_AbstractToken.GetStartLocation(), "abstract method '" + name + "' cannot be static", ErrorSeverity::Error);
	}

	Ref<FunctionDefinitionNode> FunctionDefinitionNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token first = lexer.PeekCompound();

		if (first.GetType() != TokenType::OpenBracket &&
			first.GetType() != TokenType::Const &&
			first.GetType() != TokenType::Identifier &&
			!IsAccessModifier(first.GetType()) &&
			first.GetType() != TokenType::Static &&
			first.GetType() != TokenType::Virtual &&
			first.GetType() != TokenType::Template &&
			first.GetType() != TokenType::FunPtr)
		{
			return nullptr;
		}

		auto undo = LexerUndoScope(lexer);
		state.GetErrorTracker().SuppressErrors();

		auto node = CreateRef<FunctionDefinitionNode>();
		node->ParseBody(state);

	if (!node->m_Name || node->m_Name->GetSegments().Empty())
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	if (node->m_OpenParenToken.GetType() != TokenType::OpenParen)
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return node;
	}

	void FunctionDefinitionNode::ParseTemplateHeaders(ParserState& state, TypeParameterScope& typeParameters)
	{
		// `template<T> template<U> R S<T>::F(U u)`: the class template's header, then the member template's own.
		m_TemplateDefinition = CreateRef<TemplateDefinitionNode>();
		m_TemplateDefinition->ParseBody(state);
		m_TemplateDefinition->BringTypeParametersInto(typeParameters);
		if (state.GetLexer().PeekCompound().GetType() != TokenType::Template) return;
		m_OwnerTemplateDefinition = m_TemplateDefinition;
		m_TemplateDefinition = CreateRef<TemplateDefinitionNode>();
		m_TemplateDefinition->ParseBody(state);
		m_TemplateDefinition->BringTypeParametersInto(typeParameters);
	}

	void FunctionDefinitionNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();
		TypeParameterScope typeParameters(state);

		m_Annotations.Clear();
		m_AccessModifier = AccessModifier::None;
		m_IsStatic = false;
		m_IsVirtual = false;
		m_IsOverride = false;
		m_IsFinal = false;
		m_IsAbstract = false;
		m_ReturnType = nullptr;
		m_Name = nullptr;
		m_TemplateDefinition = nullptr;
		m_OwnerTemplateDefinition = nullptr;
		m_Parameters.Clear();
		m_IsVariadic = false;
		m_IsConstMethod = false;
		m_IsNoexcept = false;
		m_IsPureVirtual = false;
		m_Body = nullptr;

		// 1. Parse annotations
		while (true)
		{
			Token t = lexer.PeekCompound();
			if (t.GetType() != TokenType::OpenBracket || QualTypeNode::StartsReturnList(lexer))
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

		// 2a. Optional template definition PREFIX - before the access modifier, so an out-of-line
		//     templated method can be written `template<T> public void Vector::Grow(...)`. (A
		//     `template` after the access modifier is still accepted by step 4.)
		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Template)
		{
			ParseTemplateHeaders(state, typeParameters);
			if (startLocation.line == 0)
				startLocation = (m_OwnerTemplateDefinition ? m_OwnerTemplateDefinition : m_TemplateDefinition)->startLocation;
			next = lexer.PeekCompound();
		}

		// 2. Access modifier
		next = lexer.PeekCompound();
		if (IsAccessModifier(next.GetType()))
		{
			m_AccessModifier = AccessModifierOf(next.GetType());
			m_AccessModifierToken = next;
			if (startLocation.line == 0)
				startLocation = next.GetStartLocation();
			lexer.NextCompoundToken();
			next = lexer.PeekCompound();
		}

		// 3. Function modifiers: static, virtual, override, final, abstract
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
			else if (mod.GetType() == TokenType::Virtual)
			{
				m_IsVirtual = true;
				m_VirtualToken = mod;
				foundModifier = true;
			}
			else if (mod.GetType() == TokenType::Override)
			{
				m_IsOverride = true;
				m_OverrideToken = mod;
				foundModifier = true;
			}
			else if (mod.GetType() == TokenType::Identifier && mod.GetText() == "final")
			{
				m_IsFinal = true;
				m_FinalToken = mod;
				foundModifier = true;
			}
			else if (mod.GetType() == TokenType::Identifier && mod.GetText() == "abstract")
			{
				m_IsAbstract = true;
				m_AbstractToken = mod;
				foundModifier = true;
			}
			else if (mod.GetType() == TokenType::Identifier && mod.GetText() == "concurrent")
			{
				errorTracker.AddError(mod.GetStartLocation(), "'concurrent' is not a C4 modifier", ErrorSeverity::Error);
				foundModifier = true;
			}

			if (!foundModifier)
				break;

			if (startLocation.line == 0)
				startLocation = mod.GetStartLocation();
			lexer.NextCompoundToken();
		}

		// 4. Optional template definition PREFIX - before return type
		next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Template) ParseTemplateHeaders(state, typeParameters);

		// 5. Parse return type - required. Accept identifier, const, or delegate as start.
		//    Leading 'const' before the return type is consumed by QualTypeNode (e.g., `const String& GetName()`).
		next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Identifier || next.GetType() == TokenType::Const || next.GetType() == TokenType::FunPtr ||
			next.GetType() == TokenType::OpenBracket)
		{
			m_ReturnType = QualTypeNode::ParseReturnType(state); // `[float, bool] F()` returns a list (D-34)

			if (startLocation.line == 0)
				startLocation = m_ReturnType->startLocation;
		}
		else
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected return type, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}

		// 6. Parse qualified function name
		Token nameToken = lexer.PeekCompound();
		if (nameToken.GetType() == TokenType::Identifier)
		{
			m_Name = CreateRef<NamespaceQualifiedNameNode>();
			m_Name->ParseBody(state);
		}
		else if (nameToken.GetType() == TokenType::Const)
		{
			// Handle leading 'const' that QualTypeNode didn't consume (e.g., `void const GetName()`)
			lexer.NextCompoundToken();
			nameToken = lexer.PeekCompound();
			if (nameToken.GetType() == TokenType::Identifier)
			{
				m_Name = CreateRef<NamespaceQualifiedNameNode>();
				m_Name->ParseBody(state);
			}
			else
			{
				errorTracker.AddError(
					nameToken.GetStartLocation(),
					Core::StringFormat::Format("Expected function name after 'const', got '{0}'", nameToken.GetText()),
					ErrorSeverity::Error);
				return;
			}
		}
		else
		{
			errorTracker.AddError(
				nameToken.GetStartLocation(),
				Core::StringFormat::Format("Expected function name, got '{0}'", nameToken.GetText()),
				ErrorSeverity::Error);
			return;
		}

		// 7. Expect opening paren for parameter list - use compound tokens. A group that cannot be parameters, with no
		// body after it, starts a variable (`T v(1);`): left unparsed, so VariableDefinitionNode claims it.
		Token openParen = lexer.PeekCompound();
		if (openParen.GetType() == TokenType::OpenParen && !ParenGroupIsParameterList(state) && !ParenGroupIsFollowedByBody(lexer))
			return;
		if (openParen.GetType() == TokenType::OpenParen)
		{
			m_OpenParenToken = openParen;
			lexer.NextCompoundToken();

			while (true)
			{
				Token t = lexer.PeekCompound();
				if (t.GetType() == TokenType::CloseParen)
					break;

				if (t.GetType() == TokenType::Ellipsis)
				{
					m_IsVariadic = true;
					m_VariadicToken = t;
					lexer.NextCompoundToken();
					break;
				}

				auto param = ParameterNode::TryParse(state);
				if (param)
				{
					m_Parameters.Push(param);
				}
				else
				{
					errorTracker.AddError(
						t.GetStartLocation(),
						Core::StringFormat::Format("Expected parameter, got '{0}'", t.GetText()),
						ErrorSeverity::Error);
					lexer.NextCompoundToken();
					break;
				}

				Token afterParam = lexer.PeekCompound();
				if (afterParam.GetType() == TokenType::Comma)
				{
					lexer.NextCompoundToken();
				}
			}

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
					Core::StringFormat::Format("Expected ')' to close parameter list, got '{0}'", closeParen.GetText()),
					ErrorSeverity::Error);
			}
		}
		else
		{
			errorTracker.AddError(
				openParen.GetStartLocation(),
				Core::StringFormat::Format("Expected '(' for parameter list, got '{0}'", openParen.GetText()),
				ErrorSeverity::Error);
			m_OpenParenToken = openParen;
		}

	// 8. Trailing qualifiers: const, override, final, noexcept - handle any ordering
	Token afterParams = lexer.PeekCompound();
	while (true)
	{
		bool matchedQualifier = false;

		if (afterParams.GetType() == TokenType::Override)
		{
			m_IsOverride = true;
			m_OverrideToken = afterParams;
			lexer.NextCompoundToken();
			matchedQualifier = true;
		}
		else if (afterParams.GetType() == TokenType::Const)
		{
			m_IsConstMethod = true;
			m_ConstToken = afterParams;
			lexer.NextCompoundToken();
			matchedQualifier = true;
		}
		else if (afterParams.GetType() == TokenType::Identifier && afterParams.GetText() == "noexcept")
		{
			m_IsNoexcept = true;
			m_NoexceptToken = afterParams;
			lexer.NextCompoundToken();
			matchedQualifier = true;
		}
		else if (afterParams.GetType() == TokenType::Identifier && afterParams.GetText() == "final")
		{
			m_IsFinal = true;
			m_FinalToken = afterParams;
			lexer.NextCompoundToken();
			matchedQualifier = true;
		}

		if (!matchedQualifier)
			break;

		afterParams = lexer.PeekCompound();
	}

		// Pure virtual: = 0
		if (afterParams.GetType() == TokenType::Equal)
		{
			Token eqToken = afterParams;
			lexer.NextCompoundToken();
			Token zeroToken = lexer.PeekCompound();
			if (zeroToken.GetType() == TokenType::Number && zeroToken.GetText() == "0")
			{
				m_IsPureVirtual = true;
				m_PureVirtualToken = eqToken;
				lexer.NextCompoundToken();
				afterParams = lexer.PeekCompound();

				if (afterParams.GetType() == TokenType::SemiColon)
				{
					m_SemicolonToken = afterParams;
					endLocation = afterParams.GetEndLocation();
					lexer.NextCompoundToken();
				}

				m_Children.Clear();
				for (uint32 i = 0; i < m_Annotations.Count(); ++i)
					m_Children.Push(m_Annotations[i]);
				if (m_ReturnType)
					m_Children.Push(m_ReturnType);
				if (m_Name)
					m_Children.Push(m_Name);
				if (m_OwnerTemplateDefinition)
					m_Children.Push(m_OwnerTemplateDefinition);
				if (m_TemplateDefinition)
					m_Children.Push(m_TemplateDefinition);
				for (uint32 i = 0; i < m_Parameters.Count(); ++i)
					m_Children.Push(m_Parameters[i]);

				if (endLocation.line == 0 && m_Name)
					endLocation = m_Name->endLocation;

				return;
			}
			else
			{
				errorTracker.AddError(
					zeroToken.GetStartLocation(),
					Core::StringFormat::Format("Expected '0' for pure virtual function, got '{0}'", zeroToken.GetText()),
					ErrorSeverity::Error);
			}
		}

	// 8b. Optional extends clause for extension methods: `extends TypeName`
	if (afterParams.GetType() == TokenType::Extends)
	{
		m_ExtendsKeywordToken = afterParams;
		lexer.NextCompoundToken();

		Token extType = lexer.PeekCompound();
		if (extType.GetType() == TokenType::Identifier)
		{
			m_ExtendsTypeName = CreateRef<NamespaceQualifiedNameNode>();
			m_ExtendsTypeName->ParseBody(state);
		}
		else
		{
			errorTracker.AddError(
				extType.GetStartLocation(),
				Core::StringFormat::Format("Expected type name after 'extends', got '{0}'", extType.GetText()),
				ErrorSeverity::Error);
		}
		afterParams = lexer.PeekCompound();
	}

	// 9. Body (code block) or semicolon (declaration)
	Token nextAfterQualifier = lexer.PeekCompound();
	if (nextAfterQualifier.GetType() == TokenType::OpenCurl)
	{
		m_Body = CodeBlockNode::TryParse(state);
		if (m_Body)
		{
			endLocation = m_Body->endLocation;
		}
		else
		{
			errorTracker.AddError(
				nextAfterQualifier.GetStartLocation(),
				"Expected function body",
				ErrorSeverity::Error);
		}

		// Optional trailing semicolon after function body (C++ style: void Foo() {};)
		Token maybeSemi = lexer.PeekCompound();
		if (maybeSemi.GetType() == TokenType::SemiColon)
		{
			m_SemicolonToken = maybeSemi;
			endLocation = maybeSemi.GetEndLocation();
			lexer.NextCompoundToken();
		}
	}
	else if (nextAfterQualifier.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = nextAfterQualifier;
		endLocation = nextAfterQualifier.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			nextAfterQualifier.GetStartLocation(),
			Core::StringFormat::Format("Expected an opening brace for the function body, or ';' after the parameter list, got '{0}'", nextAfterQualifier.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}

		if (endLocation.line == 0)
		{
			if (m_CloseParenToken.GetType() != TokenType::None)
				endLocation = m_CloseParenToken.GetEndLocation();
			else if (m_Name)
				endLocation = m_Name->endLocation;
		}
		CheckAbstract(errorTracker);

		m_Children.Clear();
		for (uint32 i = 0; i < m_Annotations.Count(); ++i)
			m_Children.Push(m_Annotations[i]);
		if (m_ReturnType)
			m_Children.Push(m_ReturnType);
		if (m_Name)
			m_Children.Push(m_Name);
		if (m_OwnerTemplateDefinition)
			m_Children.Push(m_OwnerTemplateDefinition);
		if (m_TemplateDefinition)
			m_Children.Push(m_TemplateDefinition);
		for (uint32 i = 0; i < m_Parameters.Count(); ++i)
			m_Children.Push(m_Parameters[i]);
		if (m_Body)
			m_Children.Push(m_Body);
	}

} // namespace Scripting
