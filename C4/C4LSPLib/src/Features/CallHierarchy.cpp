#include "Features/CallHierarchy.h"
#include "Server/WrittenChildren.h"
#include "Features/Constructions.h"
#include "Features/DeclarationText.h"
#include "Features/Definition.h"
#include "Features/ReferenceScanner.h"
#include "Server/CompilationSession.h"
#include "Server/NodeLocator.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <set>

namespace C4LSP::Features
{
	using C4Compiler::NodeType;

	// Whether a 0-based position lies before the function's body: on its name, annotations or parameters.
	static bool OnSignature(const C4Compiler::AstNode& node, const Protocol::Position& position)
	{
		const auto& func = static_cast<const C4Compiler::FunctionDefinitionNode&>(node);
		if (!func.GetBody())
			return true;
		const C4Compiler::SourceLocation& body = func.GetBody()->startLocation;
		return position.line + 1 < body.line || (position.line + 1 == body.line && position.character + 1 < body.column);
	}

	const C4Compiler::AstNode* CallHierarchy::FunctionAt(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		const uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return nullptr;
		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return nullptr;
		const C4Compiler::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		const Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		if (!located.Found())
			return nullptr;
		if (const C4Compiler::AstNode* target = Definition::FindTargetDeclaration(session, sourceId, located))
			return target->Type() == NodeType::FunctionDefinition ? target : nullptr;
		for (uint32 i = located.path.Count(); i-- > 0;)
			if (located.path[i]->Type() == NodeType::FunctionDefinition)
				return OnSignature(*located.path[i], position) ? located.path[i] : nullptr;
		return nullptr;
	}

	Core::Vector<HierarchyItem> CallHierarchy::Prepare(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		Core::Vector<HierarchyItem> items;
		if (const C4Compiler::AstNode* function = FunctionAt(session, uri, position))
			items.Push(HierarchyItem::Of(session, *Definition::WithBody(session, function)));
		return items;
	}

	void CallHierarchy::AddCall(Server::CompilationSession& session, const C4Compiler::AstNode& function, const ReferenceScanner::NameRange& name, Grouped& grouped, Core::Vector<HierarchyCall>& out)
	{
		const auto [entry, added] = grouped.emplace(&function, out.Count());
		if (added)
			out.Push(HierarchyCall{ HierarchyItem::Of(session, function), {} });
		out[entry->second].fromRanges.Push(session.ToEditorRange(name.start.sourceId, name.start, name.end));
	}

	Core::Vector<HierarchyCall> CallHierarchy::Incoming(Server::CompilationSession& session, const HierarchyItem& item)
	{
		Core::Vector<HierarchyCall> out;
		const C4Compiler::AstNode* target = FunctionAt(session, item.uri, item.selectionRange.start);
		Grouped grouped;
		for (const ReferenceScanner::CallSite& call : ReferenceScanner::FindCallSites(session, target))
			AddCall(session, *call.caller, call.name, grouped, out);
		return out;
	}

	Core::Vector<HierarchyCall> CallHierarchy::Outgoing(Server::CompilationSession& session, const HierarchyItem& item)
	{
		Core::Vector<HierarchyCall> out;
		const C4Compiler::AstNode* function = FunctionAt(session, item.uri, item.selectionRange.start);
		if (!function)
			return out;
		const C4Compiler::AstNode* body = Definition::WithBody(session, function);
		Core::Vector<const C4Compiler::AstNode*> path = DeclarationText::PathTo(session, *body);
		if (path.Count() == 0)
			return out;
		path.Pop(); // CollectCalls adds the function itself
		Grouped grouped;
		CollectCalls(session, *body, path, grouped, out);
		return out;
	}

	void CallHierarchy::CollectCalls(Server::CompilationSession& session, const C4Compiler::AstNode& node, const Core::Vector<const C4Compiler::AstNode*>& pathIn, Grouped& grouped, Core::Vector<HierarchyCall>& out)
	{
		Core::Vector<const C4Compiler::AstNode*> path = pathIn;
		path.Push(&node);
		if (node.Type() == NodeType::FunctionCall)
		{
			const C4Compiler::AstNode* callee = static_cast<const C4Compiler::FunctionCallNode&>(node).GetCallee().Get();
			if (callee && ReferenceScanner::IsReferenceNode(*callee, &node))
			{
				Core::Vector<const C4Compiler::AstNode*> calleePath = path;
				calleePath.Push(callee);
				const C4Compiler::AstNode* called = session.GetResolver().ResolveReference(*callee, calleePath).declaration;
				if (called && called->Type() == NodeType::FunctionDefinition)
					AddCall(session, *Definition::WithBody(session, called), ReferenceScanner::NameOf(*callee), grouped, out);
			}
		}
		for (const Constructions::Construction& construction : Constructions::Of(node, path, session.GetResolver()))
		{
			std::set<const C4Compiler::AstNode*> creates; // a create's declarations are one function
			for (const C4Compiler::AstNode* created : Constructions::Called(session.GetCompiler().GetProgramModel(), construction))
				if (created->Type() == NodeType::FunctionDefinition && creates.insert(Definition::WithBody(session, created)).second)
					AddCall(session, *Definition::WithBody(session, created), construction.name, grouped, out);
		}
		Server::ForEachWrittenChild(node, [&](const C4Compiler::AstNode& child) { CollectCalls(session, child, path, grouped, out); });
	}
} // namespace C4LSP::Features
