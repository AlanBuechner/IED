#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"

namespace Scripting { struct AstNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// One collapsible region for `textDocument/foldingRange`. Line numbers are 0-based editor
	/// coordinates: the fold starts on `startLine` and hides lines through `endLine` (inclusive),
	/// matching how VS2022 collapses a K&R block (`void f() {` stays visible, its closing brace
	/// does not). `kind` is "comment" for comment blocks and empty for code regions.
	struct FoldingRange
	{
		uint32 startLine = 0;
		uint32 endLine = 0;
		std::string kind; // "" | "comment"
	};

	class FoldingRanges
	{
	public:
		/// Collects folding ranges for one document: AST-derived folds (functions, types,
		/// namespaces, braced blocks) from this file's nodes plus comment-block folds re-lexed
		/// from the editor text. Inlined include trees are skipped.
		static Core::Vector<FoldingRange> ForDocument(Server::CompilationSession& session, const std::string& uri);

	private:
		static void CollectNode(const Scripting::AstNode& node, uint32 sourceId, Core::Vector<FoldingRange>& out);
		static void AddCommentFolds(Server::CompilationSession& session, const std::string& uri, Core::Vector<FoldingRange>& out);
	};
} // namespace C4LSP::Features
