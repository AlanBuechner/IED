#include "Features/HierarchyItem.h"
#include "Features/DeclarationText.h"
#include "Server/CompilationSession.h"
#include "Parser/AstNode.h"

namespace C4LSP::Features
{
	HierarchyItem HierarchyItem::Of(Server::CompilationSession& session, const Scripting::AstNode& declaration)
	{
		HierarchyItem item;
		item.name = DeclarationText::NameOf(declaration);
		item.kind = DeclarationText::SymbolKindOf(declaration, DeclarationText::IsMember(session, declaration));
		const std::string owner = DeclarationText::OwnerOf(session, declaration);
		item.detail = owner.empty() ? item.name : owner + "::" + item.name;
		const Protocol::Location whole = session.ToEditorLocation(declaration.startLocation, declaration.endLocation);
		item.uri = whole.uri;
		item.range = whole.range;
		const DeclarationText::NameLocation name = DeclarationText::NameLocationOf(declaration);
		item.selectionRange = name.found ? session.ToEditorRange(declaration.startLocation.sourceId, name.simpleStart, name.end) : item.range;
		return item;
	}
} // namespace C4LSP::Features
