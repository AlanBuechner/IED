#pragma once
#include "Parser/AstNode.h"

namespace Scripting
{
	/// Abstract callback interface for AST processing.
	///
	/// Implementations receive notifications as an AST tree is traversed by an
	/// ASTWalker (or any other traversal engine).  The three hooks mirror the
	/// standard visitor pattern: a single top-level entry point, a pre-order
	/// hook that can request subtree pruning, and a post-order hook.
	struct ASTConsumer
	{
		virtual ~ASTConsumer() = default;

		/// Called once when the complete translation-unit AST is available.
		/// The `root` node is typically a NamespaceNode (the implicit file-level
		/// namespace).  Default implementation does nothing so that consumers
		/// only need to override the hooks they care about.
		virtual void HandleTranslationUnit(const Ref<AstNode>& root) {}

		/// Called before descending into `node`'s children.
		///
		/// @return true to continue visiting children; false to prune the
		///         subtree (children are skipped but PostVisit is still called).
		virtual bool PreVisit(const AstNode& node) { return true; }

		/// Called after all children of `node` have been visited (or were pruned).
		/// This hook always fires even when PreVisit returned false.
		virtual void PostVisit(const AstNode& node) {}
	};
}
