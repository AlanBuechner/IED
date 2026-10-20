#pragma once
#include "Parser/Consumer/ASTConsumer.h"
#include <string>

namespace Scripting
{
	/// ASTConsumer that produces a tree-formatted debug dump of the AST,
	/// similar to the output of a terminal `tree` command.
	///
	/// Uses `dynamic_cast` for known node types to print type-specific details;
	/// unknown node types are printed generically without breaking traversal.
	///
	/// Usage:
	///   ```cpp
	///   PrettyPrintConsumer consumer;
	///   ASTWalker::Walk(rootNode, consumer);
	///   ```
	struct PrettyPrintConsumer : public ASTConsumer
	{
		PrettyPrintConsumer();

		void HandleTranslationUnit(const Ref<AstNode>& root) override;
		bool PreVisit(const AstNode& node) override;
		void PostVisit(const AstNode& node) override;

	private:
		/// Recursively print the AST subtree with tree-drawing characters.
		void PrintTree(const Ref<AstNode>& node, const std::string& prefix, bool isLast);

		/// Build a display label for a node. Returns empty string if unknown type.
		std::string NodeLabel(const AstNode& node);
	};
}
