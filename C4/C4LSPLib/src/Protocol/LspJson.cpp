#include "Protocol/LspJson.h"
#include "Features/SemanticTokens.h"
#include <map>

namespace C4LSP::Protocol
{
	Json::Value LspJson::DiagnosticsArray(const Core::Vector<Diagnostic>& diagnostics)
	{
		Json::Value array = Json::Value::MakeArray();
		for (const auto& diagnostic : diagnostics)
			array.Append(DiagnosticObject(diagnostic));
		return array;
	}

	Json::Value LspJson::DiagnosticObject(const Diagnostic& diagnostic)
	{
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("range", RangeObject(diagnostic.range));
		obj.SetMember("severity", Json::Value::Int(static_cast<int64>(diagnostic.severity)));
		obj.SetMember("code", Json::Value::Null());
		obj.SetMember("source", Json::Value::String(Core::String(diagnostic.source)));
		obj.SetMember("message", Json::Value::String(Core::String(diagnostic.message)));
		return obj;
	}

	Json::Value LspJson::Hover(const Features::HoverResult& hover)
	{
		if (!hover.found)
			return Json::Value::Null();
		Json::Value contents = Json::Value::MakeObject();
		contents.SetMember("kind", Json::Value::String(Core::String("markdown")));
		contents.SetMember("value", Json::Value::String(Core::String(hover.contents)));
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("contents", contents);
		obj.SetMember("range", RangeObject(hover.range));
		return obj;
	}

	Json::Value LspJson::HierarchyItemObject(const Features::HierarchyItem& item)
	{
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("name", Json::Value::String(Core::String(item.name)));
		obj.SetMember("kind", Json::Value::Int(item.kind));
		obj.SetMember("detail", Json::Value::String(Core::String(item.detail)));
		obj.SetMember("uri", Json::Value::String(Core::String(item.uri)));
		obj.SetMember("range", RangeObject(item.range));
		obj.SetMember("selectionRange", RangeObject(item.selectionRange));
		return obj;
	}

	Json::Value LspJson::HierarchyItems(const Core::Vector<Features::HierarchyItem>& items)
	{
		Json::Value array = Json::Value::MakeArray();
		for (const auto& item : items)
			if (!item.uri.empty())
				array.Append(HierarchyItemObject(item));
		return array.Size() == 0 ? Json::Value::Null() : array;
	}

	Json::Value LspJson::HierarchyCalls(const Core::Vector<Features::HierarchyCall>& calls, const char* itemKey)
	{
		Json::Value array = Json::Value::MakeArray();
		for (const auto& call : calls)
		{
			if (call.item.uri.empty())
				continue;
			Json::Value ranges = Json::Value::MakeArray();
			for (const auto& range : call.fromRanges)
				ranges.Append(RangeObject(range));
			Json::Value obj = Json::Value::MakeObject();
			obj.SetMember(itemKey, HierarchyItemObject(call.item));
			obj.SetMember("fromRanges", ranges);
			array.Append(obj);
		}
		return array;
	}

	Json::Value LspJson::ExpandedMacro(const Features::MacroUses::Use& use)
	{
		if (!use.found)
			return Json::Value::Null();
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("name", Json::Value::String(Core::String(use.name)));
		obj.SetMember("expansion", Json::Value::String(Core::String(use.expansion)));
		return obj;
	}

	Range LspJson::RangeFrom(const Json::Value& range)
	{
		auto position = [](const Json::Value& value) {
			Position p;
			p.line = static_cast<uint32>(value.Member("line").GetInt(0));
			p.character = static_cast<uint32>(value.Member("character").GetInt(0));
			return p;
		};
		return Range{ position(range.Member("start")), position(range.Member("end")) };
	}

	Features::HierarchyItem LspJson::HierarchyItemFrom(const Json::Value& item)
	{
		Features::HierarchyItem out;
		out.name = item.Member("name").GetString("");
		out.kind = static_cast<int>(item.Member("kind").GetInt(0));
		out.detail = item.Member("detail").GetString("");
		out.uri = item.Member("uri").GetString("");
		out.range = RangeFrom(item.Member("range"));
		out.selectionRange = RangeFrom(item.Member("selectionRange"));
		return out;
	}

	Json::Value LspJson::Locations(const Core::Vector<Location>& locations)
	{
		// A location without a document (in a script host's interface) is not one the editor can open.
		Json::Value array = Json::Value::MakeArray();
		for (const auto& location : locations)
			if (!location.uri.empty()) array.Append(LocationObject(location));
		return array.Size() == 0 ? Json::Value::Null() : array;
	}

	Json::Value LspJson::LocationObject(const Location& location)
	{
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("uri", Json::Value::String(Core::String(location.uri)));
		obj.SetMember("range", RangeObject(location.range));
		return obj;
	}

	Json::Value LspJson::CompletionItems(const Core::Vector<Features::CompletionItem>& items)
	{
		if (items.Count() == 0)
			return Json::Value::Null();
		Json::Value array = Json::Value::MakeArray();
		for (const auto& item : items)
		{
			Json::Value obj = Json::Value::MakeObject();
			obj.SetMember("label", Json::Value::String(Core::String(item.label)));
			obj.SetMember("kind", Json::Value::Int(item.kind));
			if (!item.detail.empty())
				obj.SetMember("detail", Json::Value::String(Core::String(item.detail)));
			array.Append(obj);
		}
		return array;
	}

	Json::Value LspJson::DocumentSymbols(const Core::Vector<Features::DocumentSymbol>& symbols)
	{
		if (symbols.Count() == 0)
			return Json::Value::Null();
		Json::Value array = Json::Value::MakeArray();
		for (const auto& symbol : symbols)
			array.Append(DocumentSymbolObject(symbol));
		return array;
	}

	Json::Value LspJson::WorkspaceSymbols(const Core::Vector<Features::WorkspaceSymbol>& symbols)
	{
		if (symbols.Count() == 0)
			return Json::Value::Null();
		Json::Value array = Json::Value::MakeArray();
		for (const auto& symbol : symbols)
		{
			Json::Value obj = Json::Value::MakeObject();
			obj.SetMember("name", Json::Value::String(Core::String(symbol.name)));
			obj.SetMember("kind", Json::Value::Int(symbol.kind));
			if (!symbol.containerName.empty())
				obj.SetMember("containerName", Json::Value::String(Core::String(symbol.containerName)));
			obj.SetMember("location", LocationObject(symbol.location));
			array.Append(obj);
		}
		return array;
	}

	Json::Value LspJson::DocumentSymbolObject(const Features::DocumentSymbol& symbol)
	{
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("name", Json::Value::String(Core::String(symbol.name)));
		obj.SetMember("detail", Json::Value::String(Core::String(symbol.detail)));
		obj.SetMember("kind", Json::Value::Int(symbol.kind));
		obj.SetMember("range", RangeObject(symbol.range));
		obj.SetMember("selectionRange", RangeObject(symbol.selectionRange));
		if (symbol.children.Count() > 0)
		{
			Json::Value array = Json::Value::MakeArray();
			for (const auto& child : symbol.children)
				array.Append(DocumentSymbolObject(child));
			obj.SetMember("children", array);
		}
		return obj;
	}

	Json::Value LspJson::DocumentHighlights(const Core::Vector<DocumentHighlight>& highlights)
	{
		if (highlights.Count() == 0)
			return Json::Value::Null();
		Json::Value array = Json::Value::MakeArray();
		for (const auto& highlight : highlights)
			array.Append(DocumentHighlightObject(highlight));
		return array;
	}

	Json::Value LspJson::DocumentHighlightObject(const DocumentHighlight& highlight)
	{
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("range", RangeObject(highlight.range));
		obj.SetMember("kind", Json::Value::Int(static_cast<int64>(highlight.kind)));
		return obj;
	}

	Json::Value LspJson::SemanticTokensLegend()
	{
		Json::Value legend = Json::Value::MakeObject();

		Json::Value types = Json::Value::MakeArray();
		auto typeNames = Features::SemanticTokens::TokenTypeNames();
		for (const auto& name : typeNames)
			types.Append(Json::Value::String(Core::String(name)));
		legend.SetMember("tokenTypes", types);

		Json::Value modifiers = Json::Value::MakeArray();
		auto modifierNames = Features::SemanticTokens::ModifierNames();
		for (const auto& name : modifierNames)
			modifiers.Append(Json::Value::String(Core::String(name)));
		legend.SetMember("tokenModifiers", modifiers);

		return legend;
	}

	Json::Value LspJson::SemanticTokens(const Features::SemanticTokensResult& result)
	{
		if (result.tokens.empty())
			return Json::Value::Null();

		Json::Value data = Json::Value::MakeArray();
		uint32 prevLine = 0;
		uint32 prevChar = 0;
		for (const auto& tok : result.tokens)
		{
			// LSP encodes the token array as deltas: each entry is relative to the previous.
			if (tok.startLine < prevLine || (tok.startLine == prevLine && tok.startCharacter < prevChar))
				continue; // tokens must be in ascending order; skip any out-of-order entry
			uint32 lineDelta = tok.startLine - prevLine;
			uint32 charDelta = (lineDelta > 0) ? tok.startCharacter : (tok.startCharacter - prevChar);
			data.Append(Json::Value::UInt(lineDelta));
			data.Append(Json::Value::UInt(charDelta));
			data.Append(Json::Value::UInt(tok.length));
			data.Append(Json::Value::UInt(tok.kindId));
			data.Append(Json::Value::UInt(tok.modifiers));
			prevLine = tok.startLine;
			prevChar = tok.startCharacter;
		}

		Json::Value resultObj = Json::Value::MakeObject();
		resultObj.SetMember("data", data);
		return resultObj;
	}

	Json::Value LspJson::SignatureHelp(const Features::SignatureHelpResult& result)
	{
		if (!result.found || result.signatures.Count() == 0)
			return Json::Value::Null();

		Json::Value sigs = Json::Value::MakeArray();
		for (const auto& sig : result.signatures)
		{
			Json::Value sigObj = Json::Value::MakeObject();
			sigObj.SetMember("label", Json::Value::String(Core::String(sig.label)));

			Json::Value params = Json::Value::MakeArray();
			for (const auto& param : sig.parameters)
			{
				Json::Value p = Json::Value::MakeObject();
				p.SetMember("label", Json::Value::String(Core::String(param.label)));
				params.Append(p);
			}
			sigObj.SetMember("parameters", params);
			sigs.Append(sigObj);
		}

		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("signatures", sigs);
		obj.SetMember("activeSignature", Json::Value::Int(result.activeSignature));
		obj.SetMember("activeParameter", Json::Value::Int(result.activeParameter));
		return obj;
	}

	Json::Value LspJson::Rename(const Features::RenameResult& result, const std::string& newName)
	{
		if (!result.found || result.occurrences.Count() == 0)
			return Json::Value::Null();

		// Group edits by document URI so the client can apply them per file.
		std::map<std::string, Core::Vector<Features::RenameOccurrence>> byUri;
		for (const auto& occ : result.occurrences)
			byUri[occ.uri].Push(occ);

		Json::Value changes = Json::Value::MakeObject();
		for (const auto& [uri, occs] : byUri)
		{
			Json::Value edits = Json::Value::MakeArray();
			for (const auto& occ : occs)
			{
				Json::Value edit = Json::Value::MakeObject();
				edit.SetMember("range", RangeObject(occ.range));
				edit.SetMember("newText", Json::Value::String(Core::String(newName)));
				edits.Append(edit);
			}
			changes.SetMember(uri, edits);
		}

		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("changes", changes);
		return obj;
	}

	// textDocument/prepareRename: { range, placeholder } so the client can pre-select and pre-fill
	// the rename input. Null when the position is not on a rename-able name token (the client then
	// rejects with "The symbol cannot be renamed." instead of crashing on an empty span).
	Json::Value LspJson::PrepareRename(const Features::PrepareResult& result)
	{
		if (!result.found)
			return Json::Value::Null();

		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("range", RangeObject(result.range));
		obj.SetMember("placeholder", Json::Value::String(Core::String(result.placeholder)));
		return obj;
	}

	Json::Value LspJson::CodeActions(const Core::Vector<Features::CodeAction>& actions, const std::string& uri)
	{
		Json::Value array = Json::Value::MakeArray(); // empty array is the valid "no actions" response
		for (const auto& action : actions)
		{
			// One single-document WorkspaceEdit per action ({changes:{uri:[textEdits]}}), applied by the
			// client through the same prepareWorkspaceEdit path rename uses. Zero-width ranges insert.
			Json::Value edit = Json::Value::MakeObject();
			edit.SetMember("range", RangeObject(action.editStartEnd));
			edit.SetMember("newText", Json::Value::String(Core::String(action.newText)));

			Json::Value edits = Json::Value::MakeArray();
			edits.Append(edit);

			Json::Value changes = Json::Value::MakeObject();
			changes.SetMember(uri, edits);

			Json::Value workspaceEdit = Json::Value::MakeObject();
			workspaceEdit.SetMember("changes", changes);

			Json::Value obj = Json::Value::MakeObject();
			obj.SetMember("title", Json::Value::String(Core::String(action.title)));
			obj.SetMember("kind", Json::Value::String(Core::String(action.kind)));
		if (action.quickfix)
		{
			// Scope the quickfix to its squiggle so clients can underline exactly that range. The
			// diagnostic's range is where the error reads (the published marker), which differs from
			// the edit's range for insertion-style fixes; fall back to the edit range when unset.
			const Protocol::Range& diagRange = action.hasDiagnosticRange ? action.diagnosticRange : action.editStartEnd;
			Json::Value diag = Json::Value::MakeObject();
			diag.SetMember("range", RangeObject(diagRange));
				diag.SetMember("message", Json::Value::String(Core::String(action.diagnosticMessage)));
				if (action.diagnosticSeverity > 0)
					diag.SetMember("severity", Json::Value::Int(action.diagnosticSeverity));

				Json::Value diagnostics = Json::Value::MakeArray();
				diagnostics.Append(diag);
				obj.SetMember("diagnostics", diagnostics);
			}
			obj.SetMember("edit", workspaceEdit);
			array.Append(obj);
		}
		return array;
	}

	Json::Value LspJson::FoldingRanges(const Core::Vector<Features::FoldingRange>& ranges)
	{
		Json::Value array = Json::Value::MakeArray(); // empty array is the valid "no folds" response
		for (const auto& range : ranges)
		{
			Json::Value obj = Json::Value::MakeObject();
			obj.SetMember("start", Json::Value::Int(range.startLine));  // LSP FoldingRange fields are `start`/`end`, 0-based
			obj.SetMember("end", Json::Value::Int(range.endLine));      // inclusive, per LSP spec
			if (!range.kind.empty())
				obj.SetMember("kind", Json::Value::String(Core::String(range.kind)));
			array.Append(obj);
		}
		return array;
	}

	Json::Value LspJson::RangeObject(const Range& range)
	{
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("start", PositionObject(range.start));
		obj.SetMember("end", PositionObject(range.end));
		return obj;
	}

	Json::Value LspJson::TextEdits(const std::vector<Protocol::TextEdit>& edits)
	{
		Json::Value array = Json::Value::MakeArray();
		for (const auto& edit : edits)
		{
			Json::Value e = Json::Value::MakeObject();
			e.SetMember("range", RangeObject(edit.range));
			e.SetMember("newText", Json::Value::String(Core::String(edit.newText)));
			array.Append(e);
		}
		return array;
	}

	Json::Value LspJson::PositionObject(const Position& position)
	{
		Json::Value obj = Json::Value::MakeObject();
		obj.SetMember("line", Json::Value::Int(position.line));
		obj.SetMember("character", Json::Value::Int(position.character));
		return obj;
	}
} // namespace C4LSP::Protocol
