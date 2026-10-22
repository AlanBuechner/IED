#include "Features/CallHierarchy.h"
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
	using Scripting::NodeType;

	// Whether a 0-based position lies before the function's body: on its name, annotations or parameters.
	static bool OnSignature(const Scripting::AstNode& node, const Protocol::Position& position)
	{
		const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(node);
		if (!func.GetBody())
			return true;
		const Scripting::SourceLocation& body = func.GetBody()->startLocation;
		return position.line + 1 < body.line || (position.line + 1 == body.line && position.character + 1 < body.column);
	}

	const Scripting::AstNode* CallHierarchy::FunctionAt(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		const uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return nullptr;
		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return nullptr;
		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		const Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		if (!located.Found())
			return nullptr;
		if (const Scripting::AstNode* target = Definition::FindTargetDeclaration(session, sourceId, located))
			return target->Type() == NodeType::FunctionDefinition ? target : nullptr;
		for (uint32 i = located.path.Count(); i-- > 0;)
			if (located.path[i]->Type() == NodeType::FunctionDefinition)
				return OnSignature(*located.path[i], position) ? located.path[i] : nullptr;
		return nullptr;
	}

	Core::Vector<HierarchyItem> CallHierarchy::Prepare(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		Core::Vector<HierarchyItem> items;
		if (const Scripting::AstNode* function = FunctionAt(session, uri, position))
			items.Push(HierarchyItem::Of(session, *Definition::WithBody(session, function)));
		return items;
	}

	void CallHierarchy::AddCall(Server::CompilationSession& session, const Scripting::AstNode& function, const ReferenceScanner::NameRange& name, Grouped& grouped, Core::Vector<HierarchyCall>& out)
	{
		const auto [entry, added] = grouped.emplace(&function, out.Count());
		if (added)
			out.Push(HierarchyCall{ HierarchyItem::Of(session, function), {} });
		out[entry->second].fromRanges.Push(session.ToEditorRange(name.start.sourceId, name.start, name.end));
	}

	Core::Vector<HierarchyCall> CallHierarchy::Incoming(Server::CompilationSession& session, const HierarchyItem& item)
	{
		Core::Vector<HierarchyCall> out;
		const Scripting::AstNode* target = FunctionAt(session, item.uri, item.selectionRange.start);
		Grouped grouped;
		for (const ReferenceScanner::CallSite& call : ReferenceScanner::FindCallSites(session, target))
			AddCall(session, *call.caller, call.name, grouped, out);
		return out;
	}

	Core::Vector<HierarchyCall> CallHierarchy::Outgoing(Server::CompilationSession& session, const HierarchyItem& item)
	{
		Core::Vector<HierarchyCall> out;
		const Scripting::AstNode* function = FunctionAt(session, item.uri, item.selectionRange.start);
		if (!function)
			return out;
		const Scripting::AstNode* body = Definition::WithBody(session, function);
		Core::Vector<const Scripting::AstNode*> path = DeclarationText::PathTo(session, *body);
		if (path.Count() == 0)
			return out;
		path.Pop(); // CollectCalls adds the function itself
		Grouped grouped;
		CollectCalls(session, *body, path, grouped, out);
		return out;
	}

	void CallHierarchy::CollectCalls(Server::CompilationSession& session, const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& pathIn, Grouped& grouped, Core::Vector<HierarchyCall>& out)
	{
		Core::Vector<const Scripting::AstNode*> path = pathIn;
		path.Push(&node);
		if (node.Type() == NodeType::FunctionCall)
		{
			const Scripting::AstNode* callee = static_cast<const Scripting::FunctionCallNode&>(node).GetCallee().Get();
			if (callee && ReferenceScanner::IsReferenceNode(*callee))
			{
				Core::Vector<const Scripting::AstNode*> calleePath = path;
				calleePath.Push(callee);
				const Scripting::AstNode* called = session.GetResolver().ResolveReference(*callee, calleePath).declaration;
				if (called && called->Type() == NodeType::FunctionDefinition)
					AddCall(session, *Definition::WithBody(session, called), ReferenceScanner::NameOf(*callee), grouped, out);
			}
		}
		for (const Constructions::Construction& construction : Constructions::Of(node, path, session.GetResolver()))
		{
			std::set<const Scripting::AstNode*> creates; // a create's declarations are one function
			for (const Scripting::AstNode* created : Constructions::Called(session.GetCompiler().GetProgramModel(), construction))
				if (created->Type() == NodeType::FunctionDefinition && creates.insert(Definition::WithBody(session, created)).second)
					AddCall(session, *Definition::WithBody(session, created), construction.name, grouped, out);
		}
		for (const auto& child : node.Children())
			if (child)
				CollectCalls(session, *child.Get(), path, grouped, out);
	}
} // namespace C4LSP::Features
