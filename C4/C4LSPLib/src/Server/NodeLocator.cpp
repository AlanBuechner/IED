#include "Server/NodeLocator.h"
#include "SourceManager/Source.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"

namespace C4LSP::Server
{
	LocatedNode NodeLocator::Locate(const Scripting::Source& source, uint32 sourceId,
		const Ref<Scripting::AstNode>& root, const Protocol::Position& position)
	{
		LocatedNode result;
		if (!root)
			return result;

		size_t byteOffset = PositionToByteOffset(source, position);
		Core::Vector<const Scripting::AstNode*> path;
		Find(*root.Get(), sourceId, byteOffset, path, result);
		return result;
	}

	size_t NodeLocator::PositionToByteOffset(const Scripting::Source& source, const Protocol::Position& position)
	{
		if (position.line == 0)
			return std::min<size_t>(position.character, source.Size());

		size_t lineStart = source.LineToByteOffset(position.line + 1); // LSP 0-based -> 1-based
		size_t offset = lineStart + position.character;
		return std::min(offset, source.Size());
	}

	bool NodeLocator::IsRootNamespace(const Scripting::AstNode& node)
	{
		return node.Type() == Scripting::NodeType::Namespace &&
			static_cast<const Scripting::NamespaceNode&>(node).IsRoot();
	}

	bool NodeLocator::Contains(const Scripting::AstNode& node, size_t byteOffset)
	{
		size_t start = node.startLocation.byteOffset;
		size_t end = node.endLocation.byteOffset;
		return byteOffset >= start && (byteOffset < end || (end == 0 && byteOffset == 0));
	}

	void NodeLocator::Find(const Scripting::AstNode& node, uint32 sourceId, size_t byteOffset,
		Core::Vector<const Scripting::AstNode*>& currentPath, LocatedNode& result)
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

		auto children = node.Children();
		for (const auto& child : children)
		{
			if (!child)
				continue;
			Find(*child.Get(), sourceId, byteOffset, currentPath, result);
		}

		currentPath.Pop();
	}
} // namespace C4LSP::Server
