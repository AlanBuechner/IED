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
#include "Features/Definition.h"
#include "Features/ReferenceScanner.h"
#include <algorithm>

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

		// Target declaration: from a reference (usual case), or directly under the cursor.
		const Scripting::AstNode* targetDecl = Definition::TargetAt(session, sourceId, located, position);
		if (!targetDecl)
			return result; // not on a rename-able symbol

		result.found = true;

		// A function's declaration and its out-of-line definition are one symbol: each is renamed, and a
		// reference to either counts.
		std::vector<const Scripting::AstNode*> declarations;
		session.GetResolver().CollectFunctionDeclarations(targetDecl, declarations);
		for (const Scripting::AstNode* declaration : declarations)
		{
			NameSpan declSpan = DeclarationNameSpan(*declaration);
			if (declSpan.valid)
				result.occurrences.Push(MakeOccurrence(session, declaration->startLocation.sourceId, declSpan.start, declSpan.end));
		}
		for (const ReferenceScanner::Occurrence& occurrence : ReferenceScanner::FindEverywhere(session, targetDecl))
			if (occurrence.spellsTarget) // a construction calling a renamed create spells its type's name
				result.occurrences.Push(MakeOccurrence(session, occurrence.name.start.sourceId, occurrence.name.start, occurrence.name.end));
		// A script host's symbol is declared in its interface, which no editor document is: rename it in the host.
		for (const RenameOccurrence& occurrence : result.occurrences)
			if (occurrence.uri.empty()) return RenameResult();

		return result;
	}

	// The innermost type written at the located position, or null.
	static const Scripting::AstNode* WrittenType(const Server::LocatedNode& located)
	{
		for (uint32 i = located.path.Count(); i-- > 0;)
			if (located.path[i]->Type() == Scripting::NodeType::QualType)
				return located.path[i];
		return nullptr;
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

		// A reference under the cursor renames its target declaration's name token (the usual F2 case:
		// click a use, select the declaration-style span at that use). Otherwise fall back to a
		// declaration whose own name token covers the position. Both resolve through the same logic
		// Rename::At uses to pick its target, so prepare and rename can never disagree about the symbol.
		const Scripting::AstNode* ref = nullptr;
		if (ReferenceScanner::IsReferenceNode(*located.node))
		{
			if (session.GetResolver().ResolveReference(*located.node, located.path).declaration)
				ref = located.node;
		}

		NameSpan span{};
		if (ref)
		{
			const ReferenceScanner::NameRange name = ReferenceScanner::NameOf(*ref);
			span = { true, name.start, name.end };
		}
		else if (const Scripting::AstNode* written = WrittenType(located))
		{
			// A type written here (`LspPool` of `LspPool* lsp`): its name here, when it names a declared type.
			if (Definition::TargetAt(session, sourceId, located, position))
			{
				const ReferenceScanner::NameRange name = ReferenceScanner::NameOf(*written);
				span = { true, name.start, name.end };
			}
		}
		else
		{
			const Scripting::AstNode* targetDecl = Definition::TargetAt(session, sourceId, located, position);
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

	Rename::NameSpan Rename::DeclarationNameSpan(const Scripting::AstNode& decl)
	{
		switch (decl.Type())
		{
		case Scripting::NodeType::FunctionDefinition: {
			const auto& fn = static_cast<const Scripting::FunctionDefinitionNode&>(decl);
			if (fn.GetName())
			{
				const auto& tok = fn.GetName()->GetLastIdentifierToken();
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

} // namespace C4LSP::Features
