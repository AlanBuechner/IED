#include "Server/NodeLocator.h"
#include "Server/WrittenChildren.h"
#include "SourceManager/Source.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"

namespace C4LSP::Server
{
	LocatedNode NodeLocator::Locate(const C4Compiler::Source& source, uint32 sourceId,
		const Ref<C4Compiler::AstNode>& root, const Protocol::Position& position)
	{
		LocatedNode result;
		if (!root)
			return result;

		size_t byteOffset = PositionToByteOffset(source, position);
		Core::Vector<const C4Compiler::AstNode*> path;
		Find(*root.Get(), sourceId, byteOffset, path, result);
		return result;
	}

	size_t NodeLocator::PositionToByteOffset(const C4Compiler::Source& source, const Protocol::Position& position)
	{
		// The editor's column is the file's: on a line its macro uses changed, the compiled text's column differs (BUG-446).
		return source.LineColumnToByteOffset(position.line + 1, position.character + 1); // LSP 0-based -> 1-based
	}

	bool NodeLocator::IsRootNamespace(const C4Compiler::AstNode& node)
	{
		return node.Type() == C4Compiler::NodeType::Namespace &&
			static_cast<const C4Compiler::NamespaceNode&>(node).IsRoot();
	}

	bool NodeLocator::Contains(const C4Compiler::AstNode& node, size_t byteOffset)
	{
		size_t start = node.startLocation.byteOffset;
		size_t end = node.endLocation.byteOffset;
		return byteOffset >= start && (byteOffset < end || (end == 0 && byteOffset == 0));
	}

	void NodeLocator::Find(const C4Compiler::AstNode& node, uint32 sourceId, size_t byteOffset,
		Core::Vector<const C4Compiler::AstNode*>& currentPath, LocatedNode& result)
	{
		bool isRoot = IsRootNamespace(node);
		bool belongsToTarget = isRoot || (node.startLocation.sourceId == sourceId);
		if (!belongsToTarget)
			return; // prune an inlined include subtree from another file

		if (!isRoot && !Contains(node, byteOffset))
			return; // this node's range does not cover the position

		currentPath.Push(&node);
		// This node contains the offset; a deeper match will have a longer path and replace it.
		if (result.node == nullptr || currentPath.Count() > result.path.Count())
		{
			result.node = &node;
			result.path = currentPath;
		}

		ForEachWrittenChild(node, [&](const C4Compiler::AstNode& child) { Find(child, sourceId, byteOffset, currentPath, result); });
		currentPath.Pop();
	}
} // namespace C4LSP::Server
