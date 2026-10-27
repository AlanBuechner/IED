#include "Server/LspServer.h"
#include "Protocol/LspJson.h"
#include <chrono>
#include "Features/Diagnostics.h"
#include "Features/Hover.h"
#include "Features/Definition.h"
#include "Features/References.h"
#include "Features/DocumentHighlight.h"
#include "Features/Completion.h"
#include "Features/DocumentSymbols.h"
#include "Features/SemanticTokens.h"
#include "Features/SignatureHelp.h"
#include "Features/TypeDefinition.h"
#include "Features/Implementation.h"
#include "Features/Rename.h"
#include "Features/CodeActions.h"
#include "Features/FoldingRanges.h"
#include "Features/Formatting.h"
#include "Features/Tidy.h"
#include "Features/CallHierarchy.h"
#include "Features/TypeHierarchy.h"
#include "Features/MacroUses.h"
#include "Features/WorkspaceSymbols.h"
#include <optional>
#include <set>

namespace C4LSP::Server
{
	namespace
	{
		std::string RangeKey(const Protocol::Range& range)
		{
			return std::to_string(range.start.line) + ":" + std::to_string(range.start.character) + "-" + std::to_string(range.end.line) + ":" + std::to_string(range.end.character);
		}

		std::string PlaceKey(const std::string& uri, const Protocol::Range& range)
		{
			return ComparablePath(Protocol::DocumentUri::FromUri(uri)) + "#" + RangeKey(range);
		}

		// `more` after `into`, without what `into` has already (`key` says when two are the same).
		template <typename T, typename Key>
		void AppendNew(Core::Vector<T>& into, const Core::Vector<T>& more, Key key)
		{
			std::set<std::string> seen;
			for (const T& known : into) seen.insert(key(known));
			for (const T& candidate : more)
				if (seen.insert(key(candidate)).second) into.Push(candidate);
		}

		std::string LocationKey(const Protocol::Location& location) { return PlaceKey(location.uri, location.range); }
		std::string OccurrenceKey(const Features::RenameOccurrence& occurrence) { return PlaceKey(occurrence.uri, occurrence.range); }
		std::string ItemKey(const Features::HierarchyItem& item) { return PlaceKey(item.uri, item.selectionRange); }
		std::string CallKey(const Features::HierarchyCall& call) { return ItemKey(call.item); }
		std::string SymbolKey(const Features::WorkspaceSymbol& symbol) { return symbol.name + "@" + LocationKey(symbol.location); }

		// What a query at a place asks the other programs about: the place itself in an analysis that compiles its
		// document, else where its target is declared (a use in a file of a project only one program holds).
		struct Anchor
		{
			std::string uri;
			Protocol::Position position;
			std::optional<Protocol::Location> declaration; // none when the place is a declaration's own name

			std::string DeclarationUri() const { return declaration ? declaration->uri : uri; }

			std::pair<std::string, Protocol::Position> In(const CompilationSession& other) const
			{
				if (other.FindSourceIdByUri(uri) != 0 || !declaration) return { uri, position };
				return { declaration->uri, declaration->range.start };
			}
		};

		Anchor AnchorAt(CompilationSession& session, const std::string& uri, const Protocol::Position& position)
		{
			const Core::Vector<Protocol::Location> found = Features::Definition::At(session, uri, position);
			return { uri, position, found.Empty() ? std::nullopt : std::optional<Protocol::Location>(found[0]) };
		}
	}

	void LspServer::HandleHover(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest(ExtractUri(params));
		auto result = Features::Hover::At(*session, ExtractUri(params), position);
		SendResponse(id, Protocol::LspJson::Hover(result));
	}

	void LspServer::HandleDefinition(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest(ExtractUri(params));
		auto locations = Features::Definition::At(*session, ExtractUri(params), position);
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	// A declaration's references in every program that reaches it (docs/tools/03 "One server for every program").
	void LspServer::HandleReferences(const Json::Value& id, const Json::Value& params)
	{
		const std::string uri = ExtractUri(params);
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest(uri);
		const bool includeDeclaration = params.Member("context").Member("includeDeclaration").GetBool(false);
		auto locations = Features::References::At(*session, uri, position, includeDeclaration);
		if (!locations.Empty())
		{
			const Anchor anchor = AnchorAt(*session, uri, position);
			for (const auto& other : OtherAnalyses(uri, anchor.DeclarationUri()))
			{
				const auto [at, place] = anchor.In(*other);
				AppendNew(locations, Features::References::At(*other, at, place, includeDeclaration), LocationKey);
			}
		}
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	void LspServer::HandleDocumentHighlight(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest(ExtractUri(params));
		auto highlights = Features::DocumentHighlight::At(*session, ExtractUri(params), position);
		SendResponse(id, Protocol::LspJson::DocumentHighlights(highlights));
	}

	void LspServer::HandleCompletion(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest(ExtractUri(params));
		auto items = Features::Completion::At(*session, ExtractUri(params), position);
		SendResponse(id, Protocol::LspJson::CompletionItems(items));
	}

	void LspServer::HandleDocumentSymbol(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest(ExtractUri(params));
		auto symbols = Features::DocumentSymbols::ForDocument(*session, ExtractUri(params));
		SendResponse(id, Protocol::LspJson::DocumentSymbols(symbols));
	}

	// Every program's symbols (scripts are not in workspace symbols).
	void LspServer::HandleWorkspaceSymbols(const Json::Value& id, const Json::Value& params)
	{
		const std::string query = params.Member("query").GetString(""); // {query: "..."} per the LSP spec
		WaitForPendingWork(std::chrono::milliseconds(10'000));
		Core::Vector<Features::WorkspaceSymbol> symbols;
		for (const auto& session : AllAnalyses())
			AppendNew(symbols, Features::WorkspaceSymbols::ForQuery(*session, query), SymbolKey);
		SendResponse(id, Protocol::LspJson::WorkspaceSymbols(symbols));
	}

	void LspServer::HandleSemanticTokens(const Json::Value& id, const Json::Value& params)
	{
		// Answer immediately from the current snapshot - do NOT wait for a pending recompile here.
		// The old behavior blocked up to 10s (SnapshotForRequest) because clients fetch semantic tokens
		// once and cache them: answering pre-compile would leave an opened file uncolored forever. The editor's
		// provider now wires Monaco's onDidChange event to textDocument/publishDiagnostics, so the client
		// refetches every time this server (re)compiles - a pre-compile empty answer is corrected by the
		// next publish-triggered fetch within one debounce window. Blocking here only delayed first paint
		// for as long as any project recompile runs (minutes on roots with C-bind interop headers) while
		// buying nothing: the final, correct tokens arrive via refetch either way. Every other read-only
		// feature keeps SnapshotForRequest because it has no such refresh channel and a stale answer would
		// be wrong for the text the user is looking at right now (e.g. completion after typing "x.").
		const std::string uri = ExtractUri(params);
		auto session = GetSnapshot(uri);
		auto tokens = Features::SemanticTokens::ForDocument(*session, uri);
		// The data array holds columns too, where the codec does not look: a UTF-16 client's are converted here.
		if (m_Encoding == Protocol::PositionEncoding::Utf16)
		{
			Protocol::PositionCodec codec = Codec();
			for (auto& token : tokens.tokens)
			{
				const uint32 start = codec.ToClientColumn(uri, token.startLine, token.startCharacter);
				token.length = codec.ToClientColumn(uri, token.startLine, token.startCharacter + token.length) - start;
				token.startCharacter = start;
			}
		}
		SendResponse(id, Protocol::LspJson::SemanticTokens(tokens));
	}

	void LspServer::HandleSignatureHelp(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest(ExtractUri(params));
		auto result = Features::SignatureHelp::At(*session, ExtractUri(params), ExtractPosition(params));
		SendResponse(id, Protocol::LspJson::SignatureHelp(result));
	}

	void LspServer::HandleTypeDefinition(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest(ExtractUri(params));
		auto locations = Features::TypeDefinition::At(*session, ExtractUri(params), ExtractPosition(params));
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	void LspServer::HandleDeclaration(const Json::Value& id, const Json::Value& params)
	{
		// C4 has no separate declaration/definition distinction; declarations are definitions.
		auto session = SnapshotForRequest(ExtractUri(params));
		auto locations = Features::Definition::At(*session, ExtractUri(params), ExtractPosition(params));
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	// The types deriving from a type, or the methods overriding one, in every program that reaches it.
	void LspServer::HandleImplementation(const Json::Value& id, const Json::Value& params)
	{
		const std::string uri = ExtractUri(params);
		const Protocol::Position position = ExtractPosition(params);
		auto session = SnapshotForRequest(uri);
		auto locations = Features::Implementation::At(*session, uri, position);
		const Anchor anchor = AnchorAt(*session, uri, position);
		for (const auto& other : OtherAnalyses(uri, anchor.DeclarationUri()))
		{
			const auto [at, place] = anchor.In(*other);
			AppendNew(locations, Features::Implementation::At(*other, at, place), LocationKey);
		}
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	// Every program's occurrences: a rename leaves no use behind in a project that includes the declaring one.
	void LspServer::HandleRename(const Json::Value& id, const Json::Value& params)
	{
		std::string newName = params.Member("newName").GetString();
		const std::string uri = ExtractUri(params);
		const Protocol::Position position = ExtractPosition(params);
		auto session = SnapshotForRequest(uri);
		auto result = Features::Rename::At(*session, uri, position);
		if (result.found)
		{
			const Anchor anchor = AnchorAt(*session, uri, position);
			for (const auto& other : OtherAnalyses(uri, anchor.DeclarationUri()))
			{
				const auto [at, place] = anchor.In(*other);
				AppendNew(result.occurrences, Features::Rename::At(*other, at, place).occurrences, OccurrenceKey);
			}
		}
		SendResponse(id, Protocol::LspJson::Rename(result, newName));
	}

		void LspServer::HandlePrepareRename(const Json::Value& id, const Json::Value& params)
		{
			auto session = SnapshotForRequest(ExtractUri(params)); // same freshness contract as rename: the text the user sees now
			auto result = Features::Rename::PrepareAt(*session, ExtractUri(params), ExtractPosition(params));
			SendResponse(id, Protocol::LspJson::PrepareRename(result));
		}

		void LspServer::HandleFormatting(const Json::Value& id, const Json::Value& params)
		{
			std::string uri = ExtractUri(params);
			auto session = SnapshotForRequest(ExtractUri(params)); // format the document as the client last sent it

			const Json::Value options = params.Member("options");
			Features::Formatting::Options fmt;
			if (options.IsObject()) {
				fmt.tabSize = static_cast<int>(options.Member("tabSize").GetInt(4));
				fmt.insertSpaces = !options.HasMember("insertSpaces") || options.Member("insertSpaces").GetBool(true);
			}

			std::string text;
			if (session)
				text = session->GetEditorText(uri); // open document, in-memory source, or on-disk file

			auto edits = Features::Formatting::Format(text, fmt);

			// Range formatting: keep only the edits that fall inside [range.startLine, range.endLine].
			if (params.HasMember("range")) {
				uint32 startLine = static_cast<uint32>(params.Member("range").Member("start").Member("line").GetInt(0));
				uint32 endLine   = static_cast<uint32>(params.Member("range").Member("end").Member("line").GetInt(UINT32_MAX));
				std::vector<Protocol::TextEdit> filtered;
				for (auto& e : edits)
					if (e.range.start.line >= startLine && e.range.end.line <= endLine)
						filtered.push_back(std::move(e));
				edits = std::move(filtered);
			}

			SendResponse(id, Protocol::LspJson::TextEdits(edits));
		}

	void LspServer::HandleCodeAction(const Json::Value& id, const Json::Value& params)
	{
		std::string uri = ExtractUri(params);
		auto session = SnapshotForRequest(ExtractUri(params));

		Core::Vector<Features::CodeAction> actions; // empty when there is no build yet or both features are off
		if (session && session->IsCompiled())
			actions = Features::CodeActions::ForDocument(*session, uri);

		SendResponse(id, Protocol::LspJson::CodeActions(actions, uri));
	}

	void LspServer::HandleFoldingRanges(const Json::Value& id, const Json::Value& params)
	{
		std::string uri = ExtractUri(params);
		auto session = GetSnapshot(uri); // non-blocking like semantic tokens: Monaco refetches on content change and every diagnostics publish

		Core::Vector<Features::FoldingRange> ranges;
		if (session)
			ranges = Features::FoldingRanges::ForDocument(*session, uri); // empty for files that failed to parse

		SendResponse(id, Protocol::LspJson::FoldingRanges(ranges));
	}

	void LspServer::HandlePrepareCallHierarchy(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest(ExtractUri(params));
		SendResponse(id, Protocol::LspJson::HierarchyItems(Features::CallHierarchy::Prepare(*session, ExtractUri(params), ExtractPosition(params))));
	}

	void LspServer::HandleCalls(const Json::Value& id, const Json::Value& params, bool incoming)
	{
		const Features::HierarchyItem item = Protocol::LspJson::HierarchyItemFrom(params.Member("item"));
		auto session = SnapshotForRequest(item.uri);
		auto calls = incoming ? Features::CallHierarchy::Incoming(*session, item) : Features::CallHierarchy::Outgoing(*session, item);
		if (incoming) // its callers in every program that reaches it
			for (const auto& other : OtherAnalyses(item.uri, item.uri))
				AppendNew(calls, Features::CallHierarchy::Incoming(*other, item), CallKey);
		SendResponse(id, Protocol::LspJson::HierarchyCalls(calls, incoming ? "from" : "to"));
	}

	void LspServer::HandlePrepareTypeHierarchy(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest(ExtractUri(params));
		SendResponse(id, Protocol::LspJson::HierarchyItems(Features::TypeHierarchy::Prepare(*session, ExtractUri(params), ExtractPosition(params))));
	}

	void LspServer::HandleRelatedTypes(const Json::Value& id, const Json::Value& params, bool supertypes)
	{
		const Features::HierarchyItem item = Protocol::LspJson::HierarchyItemFrom(params.Member("item"));
		auto session = SnapshotForRequest(item.uri);
		auto items = supertypes ? Features::TypeHierarchy::Supertypes(*session, item) : Features::TypeHierarchy::Subtypes(*session, item);
		if (!supertypes) // the types deriving from it in every program that reaches it
			for (const auto& other : OtherAnalyses(item.uri, item.uri))
				AppendNew(items, Features::TypeHierarchy::Subtypes(*other, item), ItemKey);
		SendResponse(id, Protocol::LspJson::HierarchyItems(items));
	}

	void LspServer::HandleExpandMacro(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest(ExtractUri(params));
		SendResponse(id, Protocol::LspJson::ExpandedMacro(Features::MacroUses::At(*session, ExtractUri(params), ExtractPosition(params))));
	}
} // namespace C4LSP::Server
