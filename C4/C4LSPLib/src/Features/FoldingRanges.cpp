#include "Features/FoldingRanges.h"
#include "Server/CompilationSession.h"
#include "Parser/AstNode.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace C4LSP::Features
{
	// A fold is only useful when it hides at least one line between its first and last line.
	static bool UseFoldingRange(uint32 startLine, uint32 endLine)
	{
		return endLine > startLine;
	}

	void FoldingRanges::CollectNode(const Scripting::AstNode& node, uint32 sourceId, Core::Vector<FoldingRange>& out)
	{
		switch (node.Type())
		{
		case Scripting::NodeType::Namespace: {
			const auto& ns = static_cast<const Scripting::NamespaceNode&>(node);
			if (!ns.IsRoot() && UseFoldingRange(node.startLocation.line, node.endLocation.line))
			{
				FoldingRange range;
				range.startLine = node.startLocation.line - 1; // editor lines are 0-based
				range.endLine = node.endLocation.line - 1;     // the closing brace line hides when folded
				out.Push(range);
			}
			break;
		}
		case Scripting::NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(node);
			if (obj.HasBody() && UseFoldingRange(node.startLocation.line, node.endLocation.line))
			{
				FoldingRange range;
				range.startLine = node.startLocation.line - 1;
				range.endLine = node.endLocation.line - 1;
				out.Push(range);
			}
			break;
		}
		case Scripting::NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(node);
			if (func.IsDeclaration())
				break; // prototypes have no body to fold
			auto body = func.GetBody();
			if (!body)
				break;
			// The function's FIRST line carries the signature (`void Main() {`), which VS2022 keeps
			// visible while collapsing - so start from the open brace, not the node start.
			const Scripting::SourceLocation& open = body->GetOpenBraceToken().GetStartLocation();
			if (open.line < 1 || !UseFoldingRange(open.line, func.endLocation.line))
				break;
			FoldingRange range;
			range.startLine = open.line - 1;
			range.endLine = func.endLocation.line - 1; // the closing brace line hides when folded
			out.Push(range);
			break;
		}
		case Scripting::NodeType::EnumDefinition: {
			const auto& en = static_cast<const Scripting::EnumDefinitionNode&>(node);
			if (UseFoldingRange(node.startLocation.line, node.endLocation.line))
			{
				FoldingRange range;
				range.startLine = node.startLocation.line - 1;
				range.endLine = node.endLocation.line - 1;
				out.Push(range);
			}
			break;
		}
		case Scripting::NodeType::UnionDefinition: {
			const auto& un = static_cast<const Scripting::UnionDefinitionNode&>(node);
			if (un.HasBody() && UseFoldingRange(node.startLocation.line, node.endLocation.line))
			{
				FoldingRange range;
				range.startLine = node.startLocation.line - 1;
				range.endLine = node.endLocation.line - 1;
				out.Push(range);
			}
			break;
		}
		case Scripting::NodeType::CodeBlock: {
			const auto& block = static_cast<const Scripting::CodeBlockNode&>(node);
			if (!block.HasBraces())
				break; // single-statement bodies have nothing to fold
			const Scripting::SourceLocation& open = block.GetOpenBraceToken().GetStartLocation();
			const Scripting::SourceLocation& close = block.GetCloseBraceToken().GetStartLocation();
			if (open.line < 1 || !UseFoldingRange(open.line, close.line))
				break; // `{ }` on one line folds to nothing
			FoldingRange range;
			range.startLine = open.line - 1;
			range.endLine = close.line - 1;
			out.Push(range);
			break;
		}
		default:
			break;
		}

		for (const auto& child : node.Children())
		{
			if (!child || child->startLocation.sourceId != sourceId)
				continue; // skip inlined include trees and foreign nodes
			CollectNode(*child.Get(), sourceId, out);
		}
	}

	void FoldingRanges::AddCommentFolds(Server::CompilationSession& session, const std::string& uri, Core::Vector<FoldingRange>& out)
	{
		std::string text = session.GetEditorText(uri);
		if (text.empty())
			return;

		auto source = Scripting::Source::FromMemory(text);
		Scripting::Lexer lexer(*source.Get(), 1); // positions are mapped by line, so the id is irrelevant here
		for (;;)
		{
			const Scripting::Token& tok = lexer.NextToken();
			if (tok.GetType() == Scripting::TokenType::EndOfFile)
				break;
			bool multi = tok.GetType() == Scripting::TokenType::MultiComment;
			if (!multi)
				continue; // single-line `//` comments never fold (VS2022 parity); the lexer also swallows their trailing newline, so they are not even candidates
			const Scripting::SourceLocation& start = tok.GetStartLocation();
			const Scripting::SourceLocation& end = tok.GetEndLocation();
			if (start.line < 1 || !UseFoldingRange(start.line, end.line))
				continue; // single-line comments never fold
			FoldingRange range;
			range.startLine = start.line - 1;
			range.endLine = end.line - 1;
			range.kind = "comment";
			out.Push(range);
		}
	}

	Core::Vector<FoldingRange> FoldingRanges::ForDocument(Server::CompilationSession& session, const std::string& uri)
	{
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return {}; // parse errors: no AST to fold from (comment folds below still apply)

		Core::Vector<FoldingRange> ranges;
		CollectNode(*unit->GetRootAST().Get(), sourceId, ranges);
		AddCommentFolds(session, uri, ranges);

		// Deduplicate identical spans: a function's fold starts at its open brace, which is exactly where
		// the body CodeBlock node folds too (main() produced two identical 31-35 entries otherwise).
		Core::Vector<FoldingRange> unique;
		std::unordered_set<uint64_t> used;
		for (const auto& range : ranges)
		{
			uint64_t key = ((uint64_t)range.startLine << 32) | range.endLine;
			if (used.count(key))
				continue;
			used.insert(key);
			unique.Push(range);
		}
		return unique;
	}
} // namespace C4LSP::Features
