#pragma once
#include "Core/Core.h"
#include "Parser/AstNode.h"
#include "Protocol/LspTypes.h"
#include <cstddef>

namespace C4Compiler { class Source; }

namespace C4LSP::Server
{
	// The innermost AST node at an LSP position plus the root->node ancestor path.
	struct LocatedNode
	{
		const C4Compiler::AstNode* node = nullptr;       // null when none found
		Core::Vector<const C4Compiler::AstNode*> path;   // empty when node is null

		bool Found() const { return node != nullptr; }
	};

	// Maps an LSP position to the AST node at that position. A merged unit inlines included
	// files, so nodes carry offsets relative to their own source; only subtrees whose
	// sourceId matches the target are descended into, keeping offsets in one file's space.
	class NodeLocator
	{
	public:
		static LocatedNode Locate(const C4Compiler::Source& source, uint32 sourceId,
			const Ref<C4Compiler::AstNode>& root, const Protocol::Position& position);

	private:
		static size_t PositionToByteOffset(const C4Compiler::Source& source, const Protocol::Position& position);
		static bool IsRootNamespace(const C4Compiler::AstNode& node);
		static bool Contains(const C4Compiler::AstNode& node, size_t byteOffset);

		// Deepest node of `sourceId` containing `byteOffset`, tracking the ancestor path.
		static void Find(const C4Compiler::AstNode& node, uint32 sourceId, size_t byteOffset,
			Core::Vector<const C4Compiler::AstNode*>& currentPath, LocatedNode& result);
	};
} // namespace C4LSP::Server
