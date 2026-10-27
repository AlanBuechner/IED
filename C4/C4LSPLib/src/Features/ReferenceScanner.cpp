#include "Features/ReferenceScanner.h"
#include "Features/Constructions.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Server/WrittenChildren.h"
#include "Features/TypeText.h"
#include <algorithm>

namespace C4LSP::Features
{
	// One search: its unit, what it looks for and where the hits go.
	struct ReferenceScanner::Search
	{
		uint32 sourceId = 0;
		const Server::SemanticResolver& resolver;
		const Scripting::ProgramModel& model;
		const std::set<const Scripting::AstNode*>& targets;
		const Sink& sink;
	};

	Core::Vector<ReferenceScanner::Occurrence> ReferenceScanner::FindOccurrences(Server::CompilationSession& session, uint32 sourceId, const Scripting::AstNode* targetDecl)
	{
		Core::Vector<Occurrence> out;
		if (!targetDecl)
			return out;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return out;

		const std::set<const Scripting::AstNode*> targets = TargetsOf(session, targetDecl);
		const Sink sink = [&out](const Occurrence& occurrence, const Core::Vector<const Scripting::AstNode*>&) { out.Push(occurrence); };
		Walk(Search{ sourceId, session.GetResolver(), session.GetCompiler().GetProgramModel(), targets, sink }, *unit->GetRootAST().Get(), {});
		return out;
	}

	Core::Vector<ReferenceScanner::Occurrence> ReferenceScanner::FindEverywhere(Server::CompilationSession& session, const Scripting::AstNode* targetDecl)
	{
		Core::Vector<Occurrence> out;
		if (targetDecl)
			EachUnit(session, TargetsOf(session, targetDecl),
				[&out](const Occurrence& occurrence, const Core::Vector<const Scripting::AstNode*>&) { out.Push(occurrence); });
		return out;
	}

	Core::Vector<ReferenceScanner::CallSite> ReferenceScanner::FindCallSites(Server::CompilationSession& session, const Scripting::AstNode* targetDecl)
	{
		Core::Vector<CallSite> out;
		if (!targetDecl)
			return out;
		EachUnit(session, TargetsOf(session, targetDecl), [&out](const Occurrence& occurrence, const Core::Vector<const Scripting::AstNode*>& path) {
			const uint32 count = path.Count();
			const bool callee = count >= 2 && path[count - 2]->Type() == Scripting::NodeType::FunctionCall &&
				static_cast<const Scripting::FunctionCallNode&>(*path[count - 2]).GetCallee().Get() == occurrence.node;
			if (!callee && occurrence.spellsTarget)
				return; // a use that is not a call
			for (uint32 i = count; i-- > 0;)
				if (path[i]->Type() == Scripting::NodeType::FunctionDefinition && path[i] != occurrence.node)
				{
					out.Push(CallSite{ occurrence.name, path[i] });
					return;
				}
		});
		return out;
	}

	void ReferenceScanner::EachUnit(Server::CompilationSession& session, const std::set<const Scripting::AstNode*>& targets, const Sink& sink)
	{
		std::set<std::pair<const Scripting::AstNode*, size_t>> seen;
		const Sink once = [&seen, &sink](const Occurrence& occurrence, const Core::Vector<const Scripting::AstNode*>& path) {
			if (seen.insert({ occurrence.node, occurrence.name.start.byteOffset }).second)
				sink(occurrence, path);
		};
		for (uint32 id = 1; id <= session.GetCompiler().SourceCount(); ++id)
		{
			auto unit = session.GetCompiler().GetCompilationUnit(id);
			if (!unit || !unit->GetRootAST())
				continue;
			Walk(Search{ id, session.GetResolver(), session.GetCompiler().GetProgramModel(), targets, once }, *unit->GetRootAST().Get(), {});
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
		case Scripting::NodeType::QualType: {
			const auto& type = static_cast<const Scripting::QualTypeNode&>(reference);
			if (type.BaseName())
				return { type.BaseName()->GetLastIdentifierToken().GetStartLocation(), type.BaseName()->GetLastIdentifierToken().GetEndLocation() };
			return { reference.startLocation, reference.endLocation };
		}
		default:
			return { reference.startLocation, reference.endLocation };
		}
	}

	namespace
	{
		bool AnyTypeTarget(const std::set<const Scripting::AstNode*>& targets)
		{
			return std::any_of(targets.begin(), targets.end(), [](const Scripting::AstNode* target) {
				return target && (target->Type() == Scripting::NodeType::ObjectDefinition || target->Type() == Scripting::NodeType::EnumDefinition ||
					target->Type() == Scripting::NodeType::UnionDefinition);
			});
		}

		bool AnyFunctionTarget(const std::set<const Scripting::AstNode*>& targets)
		{
			return std::any_of(targets.begin(), targets.end(),
				[](const Scripting::AstNode* target) { return target && target->Type() == Scripting::NodeType::FunctionDefinition; });
		}
	}

	// The qualifiers of an out-of-line definition (`Ring` of `Ring::Push`, each of `Outer::Inner::F`) that name a target.
	void ReferenceScanner::VisitQualifiers(const Search& search, const Scripting::FunctionDefinitionNode& function,
		const Core::Vector<const Scripting::AstNode*>& path)
	{
		if (!function.GetName())
			return;
		const auto& segments = function.GetName()->GetSegments();
		std::string written;
		for (uint32 i = 0; i + 1 < segments.Count(); ++i)
		{
			written += (written.empty() ? "" : "::") + std::string(segments[i].nameToken.GetText());
			const Scripting::SymbolInfo* info = search.resolver.Lookup(written, search.sourceId, &path);
			if (info && info->kind == Scripting::SymbolKind::Type && search.targets.count(info->declNode) > 0)
				search.sink(Occurrence{ &function, { segments[i].nameToken.GetStartLocation(), segments[i].nameToken.GetEndLocation() }, true }, path);
		}
	}

	// The variables a lambda's capture list names (`counter` of `[&counter]`), resolved where the lambda is written (BUG-403).
	void ReferenceScanner::VisitCaptures(const Search& search, const Scripting::LambdaNode& lambda, const Core::Vector<const Scripting::AstNode*>& path)
	{
		Core::Vector<const Scripting::AstNode*> outside = path;
		outside.Pop();
		for (const Scripting::LambdaCapture& capture : lambda.GetCaptures())
			if (capture.kind == Scripting::LambdaCapture::Kind::Variable &&
				search.targets.count(search.resolver.ResolveIdentifier(capture.name.GetText(), outside).declaration) > 0)
				search.sink(Occurrence{ &lambda, { capture.name.GetStartLocation(), capture.name.GetEndLocation() }, true }, path);
	}

	void ReferenceScanner::Walk(const Search& search, const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& pathIn)
	{
		const bool isRoot = node.Type() == Scripting::NodeType::Namespace && static_cast<const Scripting::NamespaceNode&>(node).IsRoot();

		// Prune inlined-include subtrees that belong to another file.
		if (!isRoot && node.startLocation.sourceId != search.sourceId)
			return;

		Core::Vector<const Scripting::AstNode*> path = pathIn;
		path.Push(&node);
		Visit(search, node, path);
		Server::ForEachWrittenChild(node, [&](const Scripting::AstNode& child) { Walk(search, child, path); });
	}

	// The occurrences `node` itself is: a name resolving to a target, a type written in code that names one, the
	// qualifiers of an out-of-line definition, a lambda's captures, and constructions calling a target create operator.
	void ReferenceScanner::Visit(const Search& search, const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& path)
	{
		if (IsReferenceNode(node))
		{
			if (search.targets.count(search.resolver.ResolveReference(node, path).declaration) > 0)
				search.sink(Occurrence{ &node, NameOf(node), true }, path);
			return;
		}
		if (node.Type() == Scripting::NodeType::QualType && AnyTypeTarget(search.targets))
		{
			const auto& type = static_cast<const Scripting::QualTypeNode&>(node);
			if (type.BaseName() && search.targets.count(DeclarationOfNamedType(search.resolver.ResolveQualType(type, path))) > 0)
				search.sink(Occurrence{ &node, NameOf(node), true }, path);
			return;
		}
		if (node.Type() == Scripting::NodeType::FunctionDefinition && AnyTypeTarget(search.targets))
			VisitQualifiers(search, static_cast<const Scripting::FunctionDefinitionNode&>(node), path);
		if (node.Type() == Scripting::NodeType::LambdaExpression)
			VisitCaptures(search, static_cast<const Scripting::LambdaNode&>(node), path);
		if (!AnyFunctionTarget(search.targets))
			return;
		for (const Constructions::Construction& construction : Constructions::Of(node, path, search.resolver))
			for (const Scripting::AstNode* called : Constructions::Called(search.model, construction))
				if (search.targets.count(called) > 0)
				{
					search.sink(Occurrence{ &node, construction.name, false }, path);
					break;
				}
	}
} // namespace C4LSP::Features
