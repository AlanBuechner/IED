#include "Features/ReferenceScanner.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"

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
		Walk(*unit->GetRootAST().Get(), sourceId, session.GetResolver(), path, TargetsOf(session, targetDecl),
			[&out](const Scripting::AstNode& reference, const Core::Vector<const Scripting::AstNode*>&) { out.Push(&reference); });
		return out;
	}

	Core::Vector<const Scripting::AstNode*> ReferenceScanner::FindEverywhere(Server::CompilationSession& session, const Scripting::AstNode* targetDecl)
	{
		Core::Vector<const Scripting::AstNode*> out;
		if (targetDecl)
			EachUnit(session, TargetsOf(session, targetDecl),
				[&out](const Scripting::AstNode& reference, const Core::Vector<const Scripting::AstNode*>&) { out.Push(&reference); });
		return out;
	}

	Core::Vector<ReferenceScanner::CallSite> ReferenceScanner::FindCallSites(Server::CompilationSession& session, const Scripting::AstNode* targetDecl)
	{
		Core::Vector<CallSite> out;
		if (!targetDecl)
			return out;
		EachUnit(session, TargetsOf(session, targetDecl), [&out](const Scripting::AstNode& reference, const Core::Vector<const Scripting::AstNode*>& path) {
			const uint32 count = path.Count();
			if (count < 2 || path[count - 2]->Type() != Scripting::NodeType::FunctionCall ||
				static_cast<const Scripting::FunctionCallNode&>(*path[count - 2]).GetCallee().Get() != &reference)
				return; // a use that is not the callee of a call
			for (uint32 i = count; i-- > 0;)
				if (path[i]->Type() == Scripting::NodeType::FunctionDefinition)
				{
					out.Push(CallSite{ &reference, path[i] });
					return;
				}
		});
		return out;
	}

	void ReferenceScanner::EachUnit(Server::CompilationSession& session, const std::set<const Scripting::AstNode*>& targets, const Sink& sink)
	{
		std::set<const Scripting::AstNode*> seen;
		const Sink once = [&seen, &sink](const Scripting::AstNode& reference, const Core::Vector<const Scripting::AstNode*>& path) {
			if (seen.insert(&reference).second)
				sink(reference, path);
		};
		for (uint32 id = 1; id <= session.GetCompiler().SourceCount(); ++id)
		{
			auto unit = session.GetCompiler().GetCompilationUnit(id);
			if (!unit || !unit->GetRootAST())
				continue;
			Core::Vector<const Scripting::AstNode*> path;
			Walk(*unit->GetRootAST().Get(), id, session.GetResolver(), path, targets, once);
		}
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
		const Core::Vector<const Scripting::AstNode*>& pathIn, const std::set<const Scripting::AstNode*>& targets, const Sink& sink)
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
				sink(node, path);
		}

		for (const auto& child : node.Children())
		{
			if (!child)
				continue;
			Walk(*child.Get(), sourceId, resolver, path, targets, sink);
		}
	}
} // namespace C4LSP::Features
