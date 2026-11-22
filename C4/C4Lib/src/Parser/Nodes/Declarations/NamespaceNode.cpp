#include "NamespaceNode.h"
#include "Parser/UnexpectedToken.h"
#include "Parser/Recovery.h"
#include "IncludeDeclarationNode.h"
#include "ExternDeclarationNode.h"
#include "AnnotationNode.h"
#include "AnnotationDeclarationNode.h"
#include "VariableDefinitionNode.h"
#include "FunctionDefinitionNode.h"
#include "ObjectDefinitionNode.h"
#include "EnumDefinitionNode.h"
#include "UnionDefinitionNode.h"
#include "AliasDefinitionNode.h"
#include "../Expressions/LiteralNode.h"
#include "../Statements/CodeBlockNode.h"
#include "../Statements/AsmStatementNode.h"
#include <vector>
#include "Core/StringFormat.h"

namespace C4Compiler
{
	namespace
	{
		struct DeclarationParser
		{
			Ref<AstNode> (*parse)(ParserState& state);
			bool rootOnly;
		};

		template <typename Node> Ref<AstNode> Parse(ParserState& state) { return Node::TryParse(state); }

		// What a namespace body may hold, tried in this order; includes and extern blocks only at a file's root.
		const std::vector<DeclarationParser>& DeclarationParsers()
		{
			static const std::vector<DeclarationParser> parsers = {
				{ &Parse<IncludeDeclarationNode>, true },
				{ &Parse<ExternDeclarationNode>, true },
				{ &Parse<NamespaceNode>, false },
				{ &Parse<AnnotationDeclarationNode>, false },
				{ &Parse<ObjectDefinitionNode>, false },
				{ &Parse<EnumDefinitionNode>, false },
				{ &Parse<UnionDefinitionNode>, false },
				{ &Parse<AliasDefinitionNode>, false },
				{ &Parse<AsmStatementNode>, false },
				{ &Parse<FunctionDefinitionNode>, false },
				{ &Parse<VariableDefinitionNode>, false },
				{ &Parse<CodeBlockNode>, false },
				{ &Parse<LiteralNode>, false },
			};
			return parsers;
		}
	}

	bool NamespaceNode::ParseAnnotations(ParserState& state, Core::Vector<Ref<AnnotationNode>>& annotations)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();
		if (lexer.PeekCompound().GetType() != TokenType::OpenBracket) return true;
		errorTracker.SuppressErrors();
		const TokenType after = AnnotationNode::PeekPastAnnotations(state);
		errorTracker.DiscardSuppressedErrors();
		if (after != TokenType::Namespace || lexer.PeekCompound().GetType() != TokenType::OpenBracket) return false;
		while (lexer.PeekCompound().GetType() == TokenType::OpenBracket)
		{
			auto annotation = CreateRef<AnnotationNode>();
			annotation->ParseBody(state);
			if (annotation->GetBracketGroups().Empty()) break;
			for (const auto& group : annotation->GetBracketGroups())
				for (const auto& item : group.annotations)
					if (item && item->GetName() != "reflect")
						errorTracker.AddError(item->startLocation, Core::StringFormat::Format("[{0}] cannot go on a namespace: only "
							"[reflect(...)] does, since reflection registers no namespace (docs/language/12 section 1)", item->GetName()), ErrorSeverity::Error);
			annotations.Push(annotation);
		}
		return lexer.PeekCompound().GetType() == TokenType::Namespace;
	}

	Ref<NamespaceNode> NamespaceNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		Core::Vector<Ref<AnnotationNode>> annotations;
		if (!ParseAnnotations(state, annotations)) return nullptr;
		Token token = lexer.PeekCompound();
		if (token.GetType() != TokenType::Namespace)
			return nullptr;

		lexer.NextCompoundToken(); // consume `namespace` keyword

		auto node = CreateRef<NamespaceNode>();
		node->m_IsRoot = false;
		node->m_Annotations = std::move(annotations);

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
		else
			errorTracker.AddError(close.GetStartLocation(), "Expected '}' to close the namespace body", ErrorSeverity::Error);

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

	Ref<AstNode> NamespaceNode::ParseDeclaration(ParserState& state) const
	{
		for (const DeclarationParser& parser : DeclarationParsers())
		{
			if (parser.rootOnly && !m_IsRoot) continue;
			if (auto node = parser.parse(state)) return node;
		}
		return nullptr;
	}

	// A declaration that did not parse is skipped to the next one (an ErrorNode marks it), so what follows parses on its
	// own. Tokens left over by a declaration that reported its error, or on the line of the last error, are its
	// consequence: skipped without another.
	void NamespaceNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		bool afterError = false;
		while (true)
		{
			Token token = lexer.PeekCompound();

			if (token.GetType() == TokenType::EndOfFile)
				break;

			if (!m_IsRoot && token.GetType() == TokenType::CloseCurl)
				break;

			const ErrorTracker::Mark before = errorTracker.Now();
			if (Ref<AstNode> node = ParseDeclaration(state))
			{
				m_ChildNodes.Push(node);
				afterError = errorTracker.ReportedSince(before);
				continue;
			}

			const Core::String why = UnexpectedTokenMessage(lexer);
			if (!afterError && !errorTracker.LastErrorOnLineOf(token.GetStartLocation())) errorTracker.AddError(token.GetStartLocation(), why, ErrorSeverity::Error);
			m_ChildNodes.Push(SkipBroken(lexer, Resume::Declaration, why));
			afterError = false;
		}
	}

	Core::String NamespaceNode::ToString() const
	{
		if (m_IsRoot)
			return "root namespace";
		return Core::StringFormat::Format("namespace {0}", m_NameToken.GetText());
	}

} // namespace C4Compiler
