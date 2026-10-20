#pragma once
#include "Parser/Consumer/ASTConsumer.h"
#include <stack>
#include <vector>

namespace Scripting
{
	/// Frame on the traversal stack - tracks whether we are entering (pre-visit)
	/// or leaving (post-visit) a node during DFS.
	struct WalkerFrame
	{
		const AstNode* node;   // non-null pointer into the tree
		bool isPostVisit;      // false = pre-order, true = post-order
	};

	/// Header-only, stack-based DFS AST traversal engine.
	///
	/// Walks an `AstNode` tree using `AstNode::Children()` and invokes the
	/// provided `ASTConsumer` callbacks at each node (PreVisit before children,
	/// PostVisit after).  When PreVisit returns false the subtree is pruned
	/// - children are skipped but PostVisit is still called for the parent.
	///
	/// Usage:
	///   ```cpp
	///   MyConsumer consumer;
	///   ASTWalker::Walk(rootNode, consumer);
	///   ```
	struct ASTWalker
	{
		/// Perform depth-first traversal of `root`, driving `consumer` callbacks.
		/// Does nothing if `root` is empty.
	static void Walk(const Ref<AstNode>& root, ASTConsumer& consumer)
	{
		if (!root) return;
		consumer.HandleTranslationUnit(root);
		Walk(root.Get(), consumer);
	}

	/// Walk a node already owned elsewhere (e.g. an AST subtree re-visited for template
	/// instantiation). The caller guarantees `root` stays alive for the duration of the walk.
	static void Walk(const AstNode* root, ASTConsumer& consumer)
	{
		if (!root) return;

		std::stack<WalkerFrame> stack;
		stack.push({root, false}); // start with pre-visit of root

		while (!stack.empty())
		{
			WalkerFrame frame = stack.top();
			stack.pop();

			if (frame.isPostVisit)
			{
				consumer.PostVisit(*frame.node);
				continue;
			}

			// Pre-order: ask the consumer whether to descend.
			bool traverseChildren = consumer.PreVisit(*frame.node);

			// Always schedule post-visit for this node (even if pruned).
			stack.push({frame.node, true});

			if (!traverseChildren)
				continue;

			// Push children in reverse order so they are processed left-to-right.
			auto children = frame.node->Children();
			for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i)
			{
				if (children[i])
					stack.push({children[i].Get(), false});
			}
		}
	}
	};
}
