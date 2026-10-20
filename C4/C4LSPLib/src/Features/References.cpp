#include "Features/References.h"
#include "Server/CompilationSession.h"
#include "Server/NodeLocator.h"
#include "SourceManager/Source.h"
#include "Features/Definition.h"
#include "Features/ReferenceScanner.h"

namespace C4LSP::Features
{
	Core::Vector<Protocol::Location> References::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
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
		for (const Scripting::AstNode* ref : ReferenceScanner::FindOccurrences(session, sourceId, targetDecl))
		{
			result.Push(session.ToEditorLocation(ref->startLocation, ref->endLocation));
		}
		return result;
	}
} // namespace C4LSP::Features
