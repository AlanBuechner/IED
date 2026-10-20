#include "Features/Rename.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Features/Definition.h"
#include <algorithm>
#include <set>

namespace C4LSP::Features
{
	RenameResult Rename::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		RenameResult result;
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return result;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return result;

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		if (!located.Found())
			return result;

		size_t offset = source.LineToByteOffset(position.line + 1) + position.character;
		offset = std::min(offset, source.Size());

		// Target declaration: from a reference (usual case), or directly under the cursor.
		const Scripting::AstNode* targetDecl = Definition::FindTargetDeclaration(session, sourceId, located);
		if (!targetDecl)
			targetDecl = DeclarationUnderCursor(located, offset);
		if (!targetDecl)
			return result; // not on a rename-able symbol

		result.found = true;

		NameSpan declSpan = DeclarationNameSpan(*targetDecl);
		if (declSpan.valid)
			result.occurrences.Push(MakeOccurrence(session, targetDecl->startLocation.sourceId, declSpan.start, declSpan.end));

		// Every reference to the declaration across all units, de-duplicated by node pointer:
		// merged units inline headers, so one reference node is shared by several unit roots.
		std::set<const Scripting::AstNode*> refs;
		const Server::SemanticResolver& resolver = session.GetResolver();
		for (uint32 id = 1; id <= session.GetCompiler().SourceCount(); ++id)
		{
			auto u = session.GetCompiler().GetCompilationUnit(id);
			if (!u || !u->GetRootAST())
				continue;
			Core::Vector<const Scripting::AstNode*> path;
			CollectReferences(*u->GetRootAST().Get(), id, resolver, path, targetDecl, refs);
		}
		for (const Scripting::AstNode* ref : refs)
		{
			NameSpan span = ReferenceNameSpan(*ref);
			if (span.valid)
				result.occurrences.Push(MakeOccurrence(session, ref->startLocation.sourceId, span.start, span.end));
		}

		return result;
	}

	PrepareResult Rename::PrepareAt(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		PrepareResult result;
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return result;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return result;

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		if (!located.Found())
			return result;

		size_t offset = source.LineToByteOffset(position.line + 1) + position.character;
		offset = std::min(offset, source.Size());

		// A reference under the cursor renames its target declaration's name token (the usual F2 case:
		// click a use, select the declaration-style span at that use). Otherwise fall back to a
		// declaration whose own name token covers the position. Both resolve through the same logic
		// Rename::At uses to pick its target, so prepare and rename can never disagree about the symbol.
		const Scripting::AstNode* ref = nullptr;
		if (IsReferenceNode(*located.node))
		{
			if (session.GetResolver().ResolveReference(*located.node, located.path).declaration)
				ref = located.node;
		}

		NameSpan span{};
		if (ref)
			span = ReferenceNameSpan(*ref);
		else
		{
			const Scripting::AstNode* targetDecl = Definition::FindTargetDeclaration(session, sourceId, located);
			if (!targetDecl)
				targetDecl = DeclarationUnderCursor(located, offset);
			if (targetDecl)
				span = DeclarationNameSpan(*targetDecl);
		}

		if (!span.valid || span.start.sourceId != sourceId) // rename only edits the requested document's token here
			return result;

		result.found = true;
		uint32 line, column;
		source.ByteOffsetToLineColumn(span.start.byteOffset, line, column);
		size_t length = span.end.byteOffset - span.start.byteOffset;
		result.placeholder.assign(source.Data() + span.start.byteOffset, source.Data() + span.start.byteOffset + std::min<size_t>(length, 64)); // guard: placeholder is UI text only
		result.range = session.ToEditorRange(sourceId, span.start, span.end);
		return result;
	}

	const Scripting::AstNode* Rename::DeclarationUnderCursor(const Server::LocatedNode& located, size_t offset)
	{
		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			NameSpan span = DeclarationNameSpan(*located.path[i]);
			if (span.valid && offset >= span.start.byteOffset && offset < span.end.byteOffset)
				return located.path[i];
		}
		return nullptr;
	}

	void Rename::CollectReferences(const Scripting::AstNode& node, uint32 sourceId, const Server::SemanticResolver& resolver,
		const Core::Vector<const Scripting::AstNode*>& pathIn, const Scripting::AstNode* targetDecl, std::set<const Scripting::AstNode*>& out)
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
			if (resolver.ResolveReference(node, path).declaration == targetDecl)
				out.insert(&node);
		}

		for (const auto& child : node.Children())
			if (child)
				CollectReferences(*child.Get(), sourceId, resolver, path, targetDecl, out);
	}

	Rename::NameSpan Rename::DeclarationNameSpan(const Scripting::AstNode& decl)
	{
		switch (decl.Type())
		{
		case Scripting::NodeType::FunctionDefinition: {
			const auto& fn = static_cast<const Scripting::FunctionDefinitionNode&>(decl);
			if (fn.GetName())
			{
				const auto& tok = fn.GetName()->GetFirstIdentifierToken();
				return { true, tok.GetStartLocation(), tok.GetEndLocation() };
			}
			break;
		}
		case Scripting::NodeType::VariableDefinition: {
			const auto& v = static_cast<const Scripting::VariableDefinitionNode&>(decl);
			return { true, v.GetNameToken().GetStartLocation(), v.GetNameToken().GetEndLocation() };
		}
		case Scripting::NodeType::ObjectDefinition: {
			const auto& o = static_cast<const Scripting::ObjectDefinitionNode&>(decl);
			return { true, o.GetNameToken().GetStartLocation(), o.GetNameToken().GetEndLocation() };
		}
		case Scripting::NodeType::EnumDefinition: {
			const auto& e = static_cast<const Scripting::EnumDefinitionNode&>(decl);
			return { true, e.GetNameToken().GetStartLocation(), e.GetNameToken().GetEndLocation() };
		}
		case Scripting::NodeType::Parameter: {
			const auto& p = static_cast<const Scripting::ParameterNode&>(decl);
			if (p.HasName())
				return { true, p.GetNameToken().GetStartLocation(), p.GetNameToken().GetEndLocation() };
			break;
		}
		default:
			break;
		}
		return {};
	}

	RenameOccurrence Rename::MakeOccurrence(Server::CompilationSession& session, uint32 sourceId, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end)
	{
		RenameOccurrence occ;
		occ.uri = session.UriForSourceId(sourceId);
		occ.range = session.ToEditorRange(sourceId, start, end);
		return occ;
	}

	bool Rename::IsReferenceNode(const Scripting::AstNode& node)
	{
		return node.Type() == Scripting::NodeType::IdentifierExpression ||
			node.Type() == Scripting::NodeType::MemberAccess ||
			node.Type() == Scripting::NodeType::StaticAccess;
	}

	Rename::NameSpan Rename::ReferenceNameSpan(const Scripting::AstNode& node)
	{
		switch (node.Type())
		{
		case Scripting::NodeType::IdentifierExpression: {
			const auto& id = static_cast<const Scripting::IdentifierExpressionNode&>(node);
			return { true, id.GetNameToken().GetStartLocation(), id.GetNameToken().GetEndLocation() };
		}
		case Scripting::NodeType::MemberAccess: {
			const auto& ma = static_cast<const Scripting::MemberAccessNode&>(node);
			return { true, ma.GetMemberNameToken().GetStartLocation(), ma.GetMemberNameToken().GetEndLocation() };
		}
		case Scripting::NodeType::StaticAccess: {
			const auto& sa = static_cast<const Scripting::StaticAccessNode&>(node);
			return { true, sa.GetMemberNameToken().GetStartLocation(), sa.GetMemberNameToken().GetEndLocation() };
		}
		default:
			return {};
		}
	}
} // namespace C4LSP::Features
