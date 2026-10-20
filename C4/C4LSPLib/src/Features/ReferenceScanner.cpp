#include "Features/ReferenceScanner.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"

namespace C4LSP::Features
{
	Core::Vector<const Scripting::AstNode*> ReferenceScanner::FindOccurrences(Server::CompilationSession& session, uint32 sourceId, const Scripting::AstNode* targetDecl)
	{
		Core::Vector<const Scripting::AstNode*> out;
		if (!targetDecl)
			return out;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return out;

		const Server::SemanticResolver& resolver = session.GetResolver();
		Core::Vector<const Scripting::AstNode*> path;
		Walk(*unit->GetRootAST().Get(), sourceId, resolver, path, targetDecl, out);
		return out;
	}

	bool ReferenceScanner::IsReferenceNode(const Scripting::AstNode& node)
	{
		return node.Type() == Scripting::NodeType::IdentifierExpression ||
			node.Type() == Scripting::NodeType::MemberAccess ||
			node.Type() == Scripting::NodeType::StaticAccess;
	}

	void ReferenceScanner::Walk(const Scripting::AstNode& node, uint32 sourceId, const Server::SemanticResolver& resolver,
		const Core::Vector<const Scripting::AstNode*>& pathIn, const Scripting::AstNode* targetDecl, Core::Vector<const Scripting::AstNode*>& out)
	{
		bool isRoot = node.Type() == Scripting::NodeType::Namespace &&
			static_cast<const Scripting::NamespaceNode&>(node).IsRoot();

		// Prune inlined-include subtrees that belong to another file.
		if (!isRoot && node.startLocation.sourceId != sourceId)
			return;

		Core::Vector<const Scripting::AstNode*> path = pathIn;
		path.Push(&node);

		if (IsReferenceNode(node))
		{
			const Server::NodeResolution res = resolver.ResolveReference(node, path);
			if (res.declaration == targetDecl)
				out.Push(&node);
		}

		for (const auto& child : node.Children())
		{
			if (!child)
				continue;
			Walk(*child.Get(), sourceId, resolver, path, targetDecl, out);
		}
	}
} // namespace C4LSP::Features
