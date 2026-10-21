#include "Features/References.h"
#include "Server/CompilationSession.h"
#include "Server/NodeLocator.h"
#include "SourceManager/Source.h"
#include "Features/Definition.h"
#include "Features/ReferenceScanner.h"
#include "Server/SemanticResolver.h"

namespace C4LSP::Features
{
	Core::Vector<Protocol::Location> References::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position, bool includeDeclaration)
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

		const Scripting::AstNode* targetDecl = Definition::FindTargetDeclaration(session, sourceId, located);
		if (!targetDecl)
			return {};

		Core::Vector<Protocol::Location> result;
		if (includeDeclaration)
		{
			std::vector<const Scripting::AstNode*> declarations;
			session.GetResolver().CollectFunctionDeclarations(targetDecl, declarations);
			for (const Scripting::AstNode* declaration : declarations)
				result.Push(Definition::LocationForDeclaration(session, *declaration));
		}
		for (const Scripting::AstNode* ref : ReferenceScanner::FindEverywhere(session, targetDecl))
		{
			const ReferenceScanner::NameRange name = ReferenceScanner::NameOf(*ref);
			result.Push(session.ToEditorLocation(name.start, name.end));
		}
		return result;
	}
} // namespace C4LSP::Features
