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
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Server/WrittenChildren.h"
#include <algorithm>

namespace C4LSP::Features
{
	// One search: its unit, what it looks for and where the hits go.
	struct ReferenceScanner::Search
	{
		uint32 sourceId = 0;
		const Server::SemanticResolver& resolver;
		const C4Compiler::ProgramModel& model;
		const std::set<const C4Compiler::AstNode*>& targets;
		const Sink& sink;
	};

	Core::Vector<ReferenceScanner::Occurrence> ReferenceScanner::FindOccurrences(Server::CompilationSession& session, uint32 sourceId, const C4Compiler::AstNode* targetDecl)
	{
		Core::Vector<Occurrence> out;
		if (!targetDecl)
			return out;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return out;

		const std::set<const C4Compiler::AstNode*> targets = TargetsOf(session, targetDecl);
		const Sink sink = [&out](const Occurrence& occurrence, const Core::Vector<const C4Compiler::AstNode*>&) { out.Push(occurrence); };
		Walk(Search{ sourceId, session.GetResolver(), session.GetCompiler().GetProgramModel(), targets, sink }, *unit->GetRootAST().Get(), {});
		return out;
	}

	Core::Vector<ReferenceScanner::Occurrence> ReferenceScanner::FindEverywhere(Server::CompilationSession& session, const C4Compiler::AstNode* targetDecl)
	{
		Core::Vector<Occurrence> out;
		if (targetDecl)
			EachUnit(session, TargetsOf(session, targetDecl),
				[&out](const Occurrence& occurrence, const Core::Vector<const C4Compiler::AstNode*>&) { out.Push(occurrence); });
		return out;
	}

	Core::Vector<ReferenceScanner::CallSite> ReferenceScanner::FindCallSites(Server::CompilationSession& session, const C4Compiler::AstNode* targetDecl)
	{
		Core::Vector<CallSite> out;
		if (!targetDecl)
			return out;
		EachUnit(session, TargetsOf(session, targetDecl), [&out](const Occurrence& occurrence, const Core::Vector<const C4Compiler::AstNode*>& path) {
			const uint32 count = path.Count();
			const bool callee = count >= 2 && path[count - 2]->Type() == C4Compiler::NodeType::FunctionCall &&
				static_cast<const C4Compiler::FunctionCallNode&>(*path[count - 2]).GetCallee().Get() == occurrence.node;
			if (!callee && occurrence.spellsTarget)
				return; // a use that is not a call
			for (uint32 i = count; i-- > 0;)
				if (path[i]->Type() == C4Compiler::NodeType::FunctionDefinition && path[i] != occurrence.node)
				{
					out.Push(CallSite{ occurrence.name, path[i] });
					return;
				}
		});
		return out;
	}

	void ReferenceScanner::EachUnit(Server::CompilationSession& session, const std::set<const C4Compiler::AstNode*>& targets, const Sink& sink)
	{
		std::set<std::pair<const C4Compiler::AstNode*, size_t>> seen;
		const Sink once = [&seen, &sink](const Occurrence& occurrence, const Core::Vector<const C4Compiler::AstNode*>& path) {
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

	std::set<const C4Compiler::AstNode*> ReferenceScanner::TargetsOf(Server::CompilationSession& session, const C4Compiler::AstNode* targetDecl)
	{
		std::vector<const C4Compiler::AstNode*> declarations;
		session.GetResolver().CollectFunctionDeclarations(targetDecl, declarations);
		return std::set<const C4Compiler::AstNode*>(declarations.begin(), declarations.end());
	}

	bool ReferenceScanner::IsReferenceNode(const C4Compiler::AstNode& node, const C4Compiler::AstNode* parent)
	{
		return Server::SemanticResolver::IsReference(node, parent);
	}

	const C4Compiler::AstNode* ReferenceScanner::ParentOfLast(const Core::Vector<const C4Compiler::AstNode*>& path)
	{
		return path.Count() >= 2 ? path[path.Count() - 2] : nullptr;
	}

	ReferenceScanner::NameRange ReferenceScanner::NameOf(const C4Compiler::AstNode& reference)
	{
		switch (reference.Type())
		{
		case C4Compiler::NodeType::IdentifierExpression: {
			const auto& id = static_cast<const C4Compiler::IdentifierExpressionNode&>(reference);
			return { id.GetNameToken().GetStartLocation(), id.GetNameToken().GetEndLocation() };
		}
		case C4Compiler::NodeType::MemberAccess: {
			const auto& ma = static_cast<const C4Compiler::MemberAccessNode&>(reference);
			return { ma.GetMemberNameToken().GetStartLocation(), ma.GetMemberNameToken().GetEndLocation() };
		}
		case C4Compiler::NodeType::StaticAccess: {
			const auto& sa = static_cast<const C4Compiler::StaticAccessNode&>(reference);
			return { sa.GetMemberNameToken().GetStartLocation(), sa.GetMemberNameToken().GetEndLocation() };
		}
		case C4Compiler::NodeType::AnnotationItem: {
			const auto& item = static_cast<const C4Compiler::AnnotationItem&>(reference);
			if (item.GetNameTokens().Empty())
				return { reference.startLocation, reference.endLocation };
			return { item.GetNameTokens().Back().GetStartLocation(), item.GetNameTokens().Back().GetEndLocation() };
		}
		case C4Compiler::NodeType::NamespaceQualifiedName: {
			const auto& name = static_cast<const C4Compiler::NamespaceQualifiedNameNode&>(reference);
			return { name.GetLastIdentifierToken().GetStartLocation(), name.GetLastIdentifierToken().GetEndLocation() };
		}
		case C4Compiler::NodeType::QualType: {
			const auto& type = static_cast<const C4Compiler::QualTypeNode&>(reference);
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
		bool AnyTypeTarget(const std::set<const C4Compiler::AstNode*>& targets)
		{
			return std::any_of(targets.begin(), targets.end(), [](const C4Compiler::AstNode* target) {
				return target && (target->Type() == C4Compiler::NodeType::ObjectDefinition || target->Type() == C4Compiler::NodeType::EnumDefinition ||
					target->Type() == C4Compiler::NodeType::UnionDefinition);
			});
		}

		bool AnyFunctionTarget(const std::set<const C4Compiler::AstNode*>& targets)
		{
			return std::any_of(targets.begin(), targets.end(),
				[](const C4Compiler::AstNode* target) { return target && target->Type() == C4Compiler::NodeType::FunctionDefinition; });
		}
	}

	// The qualifiers of an out-of-line definition (`Ring` of `Ring::Push`, each of `Outer::Inner::F`) that name a target.
	void ReferenceScanner::VisitQualifiers(const Search& search, const C4Compiler::FunctionDefinitionNode& function,
		const Core::Vector<const C4Compiler::AstNode*>& path)
	{
		if (!function.GetName())
			return;
		const auto& segments = function.GetName()->GetSegments();
		std::string written;
		for (uint32 i = 0; i + 1 < segments.Count(); ++i)
		{
			written += (written.empty() ? "" : "::") + std::string(segments[i].nameToken.GetText());
			const C4Compiler::SymbolInfo* info = search.resolver.Lookup(written, search.sourceId, &path);
			if (info && info->kind == C4Compiler::SymbolKind::Type && search.targets.count(info->declNode) > 0)
				search.sink(Occurrence{ &function, { segments[i].nameToken.GetStartLocation(), segments[i].nameToken.GetEndLocation() }, true }, path);
		}
	}

	// The variables a lambda's capture list names (`counter` of `[&counter]`), resolved where the lambda is written (BUG-403).
	void ReferenceScanner::VisitCaptures(const Search& search, const C4Compiler::LambdaNode& lambda, const Core::Vector<const C4Compiler::AstNode*>& path)
	{
		Core::Vector<const C4Compiler::AstNode*> outside = path;
		outside.Pop();
		for (const C4Compiler::LambdaCapture& capture : lambda.GetCaptures())
			if (capture.kind == C4Compiler::LambdaCapture::Kind::Variable &&
				search.targets.count(search.resolver.ResolveIdentifier(capture.name.GetText(), outside).declaration) > 0)
				search.sink(Occurrence{ &lambda, { capture.name.GetStartLocation(), capture.name.GetEndLocation() }, true }, path);
	}

	void ReferenceScanner::Walk(const Search& search, const C4Compiler::AstNode& node, const Core::Vector<const C4Compiler::AstNode*>& pathIn)
	{
		const bool isRoot = node.Type() == C4Compiler::NodeType::Namespace && static_cast<const C4Compiler::NamespaceNode&>(node).IsRoot();

		// Prune inlined-include subtrees that belong to another file.
		if (!isRoot && node.startLocation.sourceId != search.sourceId)
			return;

		Core::Vector<const C4Compiler::AstNode*> path = pathIn;
		path.Push(&node);
		Visit(search, node, path);
		Server::ForEachWrittenChild(node, [&](const C4Compiler::AstNode& child) { Walk(search, child, path); });
	}

	// The occurrences `node` itself is: a name resolving to a target, a type written in code that names one, the
	// qualifiers of an out-of-line definition, a lambda's captures, and constructions calling a target create operator.
	void ReferenceScanner::Visit(const Search& search, const C4Compiler::AstNode& node, const Core::Vector<const C4Compiler::AstNode*>& path)
	{
		if (IsReferenceNode(node, ParentOfLast(path)))
		{
			if (search.targets.count(search.resolver.ResolveReference(node, path).declaration) > 0)
				search.sink(Occurrence{ &node, NameOf(node), true }, path);
			return;
		}
		if (node.Type() == C4Compiler::NodeType::QualType && AnyTypeTarget(search.targets))
		{
			const auto& type = static_cast<const C4Compiler::QualTypeNode&>(node);
			if (type.BaseName() && search.targets.count(search.resolver.DeclarationOfWrittenType(type, path)) > 0)
				search.sink(Occurrence{ &node, NameOf(node), true }, path);
			return;
		}
		if (node.Type() == C4Compiler::NodeType::AnnotationItem)
		{
			const auto& item = static_cast<const C4Compiler::AnnotationItem&>(node);
			const C4Compiler::DeclaredAnnotations::Overloads overloads = search.resolver.Annotations().Find(item.GetName().c_str(),
				search.resolver.AnnotationContextAt(path, item.startLocation.sourceId));
			if (std::any_of(overloads.begin(), overloads.end(),
				[&](const C4Compiler::AnnotationSignature* overload) { return search.targets.count(overload->declaration) > 0; }))
				search.sink(Occurrence{ &node, NameOf(node), true }, path);
			return;   // its arguments are walked as children
		}
		if (node.Type() == C4Compiler::NodeType::FunctionDefinition && AnyTypeTarget(search.targets))
			VisitQualifiers(search, static_cast<const C4Compiler::FunctionDefinitionNode&>(node), path);
		if (node.Type() == C4Compiler::NodeType::LambdaExpression)
			VisitCaptures(search, static_cast<const C4Compiler::LambdaNode&>(node), path);
		if (!AnyFunctionTarget(search.targets))
			return;
		for (const Constructions::Construction& construction : Constructions::Of(node, path, search.resolver))
			for (const C4Compiler::AstNode* called : Constructions::Called(search.model, construction))
				if (search.targets.count(called) > 0)
				{
					search.sink(Occurrence{ &node, construction.name, false }, path);
					break;
				}
	}
} // namespace C4LSP::Features
