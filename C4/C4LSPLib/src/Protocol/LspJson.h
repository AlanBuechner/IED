#pragma once
#include "Json/JsonValue.h"
#include "Protocol/LspTypes.h"
#include "Features/Hover.h"
#include "Features/Completion.h"
#include "Features/DocumentSymbols.h"
#include "Features/WorkspaceSymbols.h"
#include "Features/SemanticTokens.h"
#include "Features/SignatureHelp.h"
#include "Features/Rename.h"
#include "Features/CodeActions.h"
#include "Features/FoldingRanges.h"
#include "Features/HierarchyItem.h"
#include "Features/MacroUses.h"
#include "Core/Core.h"

namespace C4LSP::Protocol
{
	/// Converts feature result structs into the exact JSON shapes LSP expects. Kept in one place
	/// so the dispatcher stays thin and the wire format is easy to audit against the spec.
	class LspJson
	{
	public:
		static Json::Value DiagnosticsArray(const Core::Vector<Diagnostic>& diagnostics);
		static Json::Value Hover(const Features::HoverResult& hover);          // object or null
		static Json::Value Locations(const Core::Vector<Location>& locations);  // array or null
		static Json::Value CompletionItems(const Core::Vector<Features::CompletionItem>& items); // array or null
		static Json::Value DocumentSymbols(const Core::Vector<Features::DocumentSymbol>& symbols);
		static Json::Value WorkspaceSymbols(const Core::Vector<Features::WorkspaceSymbol>& symbols); // array or null (workspace/symbol)
		static Json::Value DocumentHighlights(const Core::Vector<DocumentHighlight>& highlights); // array or null
		static Json::Value SemanticTokensLegend();                                                // { tokenTypes, tokenModifiers }
		static Json::Value SemanticTokens(const Features::SemanticTokensResult& result);          // object or null
		static Json::Value SignatureHelp(const Features::SignatureHelpResult& result);            // object or null
		static Json::Value Rename(const Features::RenameResult& result, const std::string& newName); // WorkspaceEdit or null
	static Json::Value PrepareRename(const Features::PrepareResult& result);                    // {range, placeholder} or null
		static Json::Value TextEdits(const std::vector<Protocol::TextEdit>& edits);                 // array of LSP textEdits (empty = [])
		static Json::Value CodeActions(const Core::Vector<Features::CodeAction>& actions, const std::string& uri); // array (empty = [])
		static Json::Value FoldingRanges(const Core::Vector<Features::FoldingRange>& ranges);         // array of LSP foldingRanges (empty = [])
		static Json::Value HierarchyItems(const Core::Vector<Features::HierarchyItem>& items);       // array or null (prepare*Hierarchy, supertypes, subtypes)
		static Json::Value HierarchyCalls(const Core::Vector<Features::HierarchyCall>& calls, const char* itemKey); // [{from|to, fromRanges}]
		static Json::Value ExpandedMacro(const Features::MacroUses::Use& use);                         // {name, expansion} or null (c4/expandMacro)
		static Features::HierarchyItem HierarchyItemFrom(const Json::Value& item);                    // a client's item back

private:
		static Json::Value DiagnosticObject(const Diagnostic& diagnostic);
		static Json::Value LocationObject(const Location& location);
		static Json::Value RangeObject(const Range& range);
		static Json::Value PositionObject(const Position& position);
		static Json::Value DocumentSymbolObject(const Features::DocumentSymbol& symbol);
		static Json::Value HierarchyItemObject(const Features::HierarchyItem& item);
		static Range RangeFrom(const Json::Value& range);
		static Json::Value DocumentHighlightObject(const DocumentHighlight& highlight);
	};
} // namespace C4LSP::Protocol
