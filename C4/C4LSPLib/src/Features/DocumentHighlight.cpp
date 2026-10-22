#include "Features/DocumentHighlight.h"
#include "Server/CompilationSession.h"
#include "Server/NodeLocator.h"
#include "SourceManager/Source.h"
#include "Features/Definition.h"
#include "Features/ReferenceScanner.h"
#include "Server/SemanticResolver.h"

namespace C4LSP::Features
{
	Core::Vector<Protocol::DocumentHighlight> DocumentHighlight::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return {};

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		if (!located.Found())
			return {};

		const Scripting::AstNode* targetDecl = Definition::TargetAt(session, sourceId, located, position);
		if (!targetDecl)
			return {};

		// Visual Studio marks the DECLARED NAME of a symbol as Write and every other occurrence Read.
		// referenceDeclarations records only USES (expression nodes), so FindOccurrences below never
		// contains the declaration site itself - add it explicitly from LocationForDeclaration, which
		// already resolves exactly that name-token location (function qualified-name identifier;
		// GetNameToken() for variable/object/enum definitions). When the symbol is declared in another
		// file its Write highlight belongs to THAT document and is skipped here.
		// A function's declaration and its out-of-line definition are each a declaration site.
		Core::Vector<Protocol::DocumentHighlight> result;
		std::vector<const Scripting::AstNode*> declarations;
		session.GetResolver().CollectFunctionDeclarations(targetDecl, declarations);
		for (const Scripting::AstNode* declaration : declarations)
		{
			const Protocol::Location declaredName = Definition::LocationForDeclaration(session, *declaration);
			if (declaredName.uri != uri)
				continue;
			Protocol::DocumentHighlight declarationSite;
			declarationSite.range = declaredName.range; // same unit: already editor coords
			declarationSite.kind = Protocol::HighlightKind::Write;
			result.Push(declarationSite);
		}

		for (const ReferenceScanner::Occurrence& occurrence : ReferenceScanner::FindOccurrences(session, sourceId, targetDecl))
		{
			Protocol::DocumentHighlight highlight;
			highlight.range = session.ToEditorRange(occurrence.name.start.sourceId, occurrence.name.start, occurrence.name.end);
			highlight.kind = Protocol::HighlightKind::Read; // uses only - the declaration was handled above
			result.Push(highlight);
		}
		return result;
	}
} // namespace C4LSP::Features
