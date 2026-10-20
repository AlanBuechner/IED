#include "NamespaceNode.h"
#include "IncludeDeclarationNode.h"
#include "ExternDeclarationNode.h"
#include "AnnotationNode.h"
#include "VariableDefinitionNode.h"
#include "FunctionDefinitionNode.h"
#include "ObjectDefinitionNode.h"
#include "EnumDefinitionNode.h"
#include "UnionDefinitionNode.h"
#include "AliasDefinitionNode.h"
#include "../Expressions/LiteralNode.h"
#include "../Statements/CodeBlockNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{

	Ref<NamespaceNode> NamespaceNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		Token token = lexer.PeekCompound();
		if (token.GetType() != TokenType::Namespace)
			return nullptr;

		lexer.NextCompoundToken(); // consume `namespace` keyword

		auto node = CreateRef<NamespaceNode>();
		node->m_IsRoot = false;

		Token nameToken = lexer.PeekCompound();
		bool nameWasMissing = false;
		if (nameToken.GetType() != TokenType::Identifier)
		{
			errorTracker.AddError(
				nameToken.GetStartLocation(),
				Core::StringFormat::Format("Expected identifier for namespace name, got '{0}'", nameToken.GetText()),
				ErrorSeverity::Error);
			nameWasMissing = true;
		}
		else
		{
			node->startLocation = nameToken.GetStartLocation();
			node->m_NameToken = nameToken;
			lexer.NextCompoundToken();
		}

		// `namespace A::B { }` is `namespace A { namespace B { } }` (D-51): each further name nests a node, and
		// the body belongs to the innermost one.
		Core::Vector<Ref<NamespaceNode>> nested = nameWasMissing ? Core::Vector<Ref<NamespaceNode>>() : ParseNestedNames(state);
		NamespaceNode* body = nested.Empty() ? node.Get() : nested.Back().Get();
		for (uint32 i = 0; i < nested.Count(); ++i)
			(i == 0 ? node.Get() : nested[i - 1].Get())->m_ChildNodes.Push(nested[i]);

		Token brace = lexer.PeekCompound();
		if (brace.GetType() != TokenType::OpenCurl)
		{
			if (!nameWasMissing)
			{
				errorTracker.AddError(
					brace.GetStartLocation(),
					Core::StringFormat::Format("Expected an opening brace for the namespace body, got '{0}'", brace.GetText()),
					ErrorSeverity::Error);
			}

			bool foundBrace = false;
			while (true)
			{
				Token t = lexer.PeekCompound();
				if (t.GetType() == TokenType::OpenCurl)
				{
					lexer.NextCompoundToken();
					foundBrace = true;
					break;
				}
				if (t.GetType() == TokenType::EndOfFile || t.GetType() == TokenType::CloseCurl)
					break;
				lexer.NextCompoundToken();
			}

			if (!foundBrace)
				return node;
		}
		else
		{
			lexer.NextCompoundToken();
		}

		body->ParseBody(state);

		Token close = lexer.PeekCompound();
		if (close.GetType() == TokenType::CloseCurl)
		{
			node->endLocation = close.GetEndLocation();
			for (auto& inner : nested) inner->endLocation = close.GetEndLocation();
			lexer.NextCompoundToken();
		}

		return node;
	}

	Core::Vector<Ref<NamespaceNode>> NamespaceNode::ParseNestedNames(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		Core::Vector<Ref<NamespaceNode>> nested;
		while (lexer.PeekCompound().GetType() == TokenType::DoubleColon)
		{
			lexer.NextCompoundToken();
			Token name = lexer.PeekCompound();
			if (name.GetType() != TokenType::Identifier)
			{
				state.GetErrorTracker().AddError(name.GetStartLocation(),
					Core::StringFormat::Format("Expected a namespace name after '::', got '{0}'", name.GetText()), ErrorSeverity::Error);
				break;
			}
			auto inner = CreateRef<NamespaceNode>();
			inner->startLocation = name.GetStartLocation();
			inner->m_NameToken = name;
			lexer.NextCompoundToken();
			nested.Push(inner);
		}
		return nested;
	}

	void NamespaceNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		while (true)
		{
			Token token = lexer.PeekCompound();

			if (token.GetType() == TokenType::EndOfFile)
				break;

			if (!m_IsRoot && token.GetType() == TokenType::CloseCurl)
				break;

			bool matched = false;

			if (m_IsRoot)
			{
				auto node = IncludeDeclarationNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

			if (!matched && m_IsRoot)
			{
				auto node = ExternDeclarationNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

			if (!matched)
			{
				auto node = NamespaceNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

			if (!matched)
			{
				auto node = ObjectDefinitionNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

			if (!matched)
			{
				auto node = EnumDefinitionNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

		if (!matched)
		{
			auto node = UnionDefinitionNode::TryParse(state);
			if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
		}

			if (!matched)
			{
				auto node = AliasDefinitionNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

		if (!matched)
		{
			auto node = FunctionDefinitionNode::TryParse(state);
			if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
		}

			if (!matched)
			{
				auto node = VariableDefinitionNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

			if (!matched)
			{
				auto node = CodeBlockNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

			if (!matched)
			{
				auto node = LiteralNode::TryParse(state);
				if(node != nullptr) { m_ChildNodes.Push(node); matched = true; }
			}

			if (matched)
				continue;

			errorTracker.AddError(
				token.GetStartLocation(),
				Core::StringFormat::Format("Unexpected token '{0}'", token.GetText()),
				ErrorSeverity::Error);
			lexer.NextCompoundToken();
		}
	}

	Core::String NamespaceNode::ToString() const
	{
		if (m_IsRoot)
			return "root namespace";
		return Core::StringFormat::Format("namespace {0}", m_NameToken.GetText());
	}

} // namespace Scripting
