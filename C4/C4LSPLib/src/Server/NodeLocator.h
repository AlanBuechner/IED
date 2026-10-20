#pragma once
#include "Core/Core.h"
#include "Parser/AstNode.h"
#include "Protocol/LspTypes.h"
#include <cstddef>

namespace Scripting { class Source; }

namespace C4LSP::Server
{
	// The innermost AST node at an LSP position plus the root->node ancestor path.
	struct LocatedNode
	{
		const Scripting::AstNode* node = nullptr;       // null when none found
		Core::Vector<const Scripting::AstNode*> path;   // empty when node is null

		bool Found() const { return node != nullptr; }
	};

	// Maps an LSP position to the AST node at that position. A merged unit inlines included
	// files, so nodes carry offsets relative to their own source; only subtrees whose
	// sourceId matches the target are descended into, keeping offsets in one file's space.
	class NodeLocator
	{
	public:
		static LocatedNode Locate(const Scripting::Source& source, uint32 sourceId,
			const Ref<Scripting::AstNode>& root, const Protocol::Position& position);

	private:
		static size_t PositionToByteOffset(const Scripting::Source& source, const Protocol::Position& position);
		static bool IsRootNamespace(const Scripting::AstNode& node);
		static bool Contains(const Scripting::AstNode& node, size_t byteOffset);

		// Deepest node of `sourceId` containing `byteOffset`, tracking the ancestor path.
		static void Find(const Scripting::AstNode& node, uint32 sourceId, size_t byteOffset,
			Core::Vector<const Scripting::AstNode*>& currentPath, LocatedNode& result);
	};
} // namespace C4LSP::Server
