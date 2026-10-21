#include "Features/ReferenceScanner.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"

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

		Core::Vector<const Scripting::AstNode*> path;
		Walk(*unit->GetRootAST().Get(), sourceId, session.GetResolver(), path, TargetsOf(session, targetDecl), out);
		return out;
	}

	Core::Vector<const Scripting::AstNode*> ReferenceScanner::FindEverywhere(Server::CompilationSession& session, const Scripting::AstNode* targetDecl)
	{
		Core::Vector<const Scripting::AstNode*> out;
		if (!targetDecl)
			return out;

		const std::set<const Scripting::AstNode*> targets = TargetsOf(session, targetDecl);
		std::set<const Scripting::AstNode*> seen;
		for (uint32 id = 1; id <= session.GetCompiler().SourceCount(); ++id)
		{
			auto unit = session.GetCompiler().GetCompilationUnit(id);
			if (!unit || !unit->GetRootAST())
				continue;
			Core::Vector<const Scripting::AstNode*> path;
			Core::Vector<const Scripting::AstNode*> found;
			Walk(*unit->GetRootAST().Get(), id, session.GetResolver(), path, targets, found);
			for (const Scripting::AstNode* node : found)
				if (seen.insert(node).second)
					out.Push(node);
		}
		return out;
	}

	std::set<const Scripting::AstNode*> ReferenceScanner::TargetsOf(Server::CompilationSession& session, const Scripting::AstNode* targetDecl)
	{
		std::vector<const Scripting::AstNode*> declarations;
		session.GetResolver().CollectFunctionDeclarations(targetDecl, declarations);
		return std::set<const Scripting::AstNode*>(declarations.begin(), declarations.end());
	}

	bool ReferenceScanner::IsReferenceNode(const Scripting::AstNode& node)
	{
		return node.Type() == Scripting::NodeType::IdentifierExpression ||
			node.Type() == Scripting::NodeType::MemberAccess ||
			node.Type() == Scripting::NodeType::StaticAccess;
	}

	ReferenceScanner::NameRange ReferenceScanner::NameOf(const Scripting::AstNode& reference)
	{
		switch (reference.Type())
		{
		case Scripting::NodeType::IdentifierExpression: {
			const auto& id = static_cast<const Scripting::IdentifierExpressionNode&>(reference);
			return { id.GetNameToken().GetStartLocation(), id.GetNameToken().GetEndLocation() };
		}
		case Scripting::NodeType::MemberAccess: {
			const auto& ma = static_cast<const Scripting::MemberAccessNode&>(reference);
			return { ma.GetMemberNameToken().GetStartLocation(), ma.GetMemberNameToken().GetEndLocation() };
		}
		case Scripting::NodeType::StaticAccess: {
			const auto& sa = static_cast<const Scripting::StaticAccessNode&>(reference);
			return { sa.GetMemberNameToken().GetStartLocation(), sa.GetMemberNameToken().GetEndLocation() };
		}
		default:
			return { reference.startLocation, reference.endLocation };
		}
	}

	void ReferenceScanner::Walk(const Scripting::AstNode& node, uint32 sourceId, const Server::SemanticResolver& resolver,
		const Core::Vector<const Scripting::AstNode*>& pathIn, const std::set<const Scripting::AstNode*>& targets, Core::Vector<const Scripting::AstNode*>& out)
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
			if (targets.count(res.declaration) > 0)
				out.Push(&node);
		}

		for (const auto& child : node.Children())
		{
			if (!child)
				continue;
			Walk(*child.Get(), sourceId, resolver, path, targets, out);
		}
	}
} // namespace C4LSP::Features
