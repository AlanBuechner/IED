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

namespace C4LSP::Server
{
	void LspServer::HandleHover(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest();
		auto result = Features::Hover::At(*session, ExtractUri(params), position);
		SendResponse(id, Protocol::LspJson::Hover(result));
	}

	void LspServer::HandleDefinition(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest();
		auto locations = Features::Definition::At(*session, ExtractUri(params), position);
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	void LspServer::HandleReferences(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest();
		const bool includeDeclaration = params.Member("context").Member("includeDeclaration").GetBool(false);
		auto locations = Features::References::At(*session, ExtractUri(params), position, includeDeclaration);
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	void LspServer::HandleDocumentHighlight(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest();
		auto highlights = Features::DocumentHighlight::At(*session, ExtractUri(params), position);
		SendResponse(id, Protocol::LspJson::DocumentHighlights(highlights));
	}

	void LspServer::HandleCompletion(const Json::Value& id, const Json::Value& params)
	{
		auto position = ExtractPosition(params);
		auto session = SnapshotForRequest();
		auto items = Features::Completion::At(*session, ExtractUri(params), position);
		SendResponse(id, Protocol::LspJson::CompletionItems(items));
	}

	void LspServer::HandleDocumentSymbol(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest();
		auto symbols = Features::DocumentSymbols::ForDocument(*session, ExtractUri(params));
		SendResponse(id, Protocol::LspJson::DocumentSymbols(symbols));
	}

	void LspServer::HandleWorkspaceSymbols(const Json::Value& id, const Json::Value& params)
	{
		const std::string query = params.Member("query").GetString(""); // {query: "..."} per the LSP spec
		auto session = SnapshotForRequest();
		auto symbols = Features::WorkspaceSymbols::ForQuery(*session, query);
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
		auto session = GetSnapshot();
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
		auto session = SnapshotForRequest();
		auto result = Features::SignatureHelp::At(*session, ExtractUri(params), ExtractPosition(params));
		SendResponse(id, Protocol::LspJson::SignatureHelp(result));
	}

	void LspServer::HandleTypeDefinition(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest();
		auto locations = Features::TypeDefinition::At(*session, ExtractUri(params), ExtractPosition(params));
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	void LspServer::HandleDeclaration(const Json::Value& id, const Json::Value& params)
	{
		// C4 has no separate declaration/definition distinction; declarations are definitions.
		auto session = SnapshotForRequest();
		auto locations = Features::Definition::At(*session, ExtractUri(params), ExtractPosition(params));
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

	void LspServer::HandleImplementation(const Json::Value& id, const Json::Value& params)
	{
		auto session = SnapshotForRequest();
		auto locations = Features::Implementation::At(*session, ExtractUri(params), ExtractPosition(params));
		SendResponse(id, Protocol::LspJson::Locations(locations));
	}

		void LspServer::HandleRename(const Json::Value& id, const Json::Value& params)
		{
			std::string newName = params.Member("newName").GetString();
			auto session = SnapshotForRequest();
			auto result = Features::Rename::At(*session, ExtractUri(params), ExtractPosition(params));
			SendResponse(id, Protocol::LspJson::Rename(result, newName));
		}

		void LspServer::HandlePrepareRename(const Json::Value& id, const Json::Value& params)
		{
			auto session = SnapshotForRequest(); // same freshness contract as rename: the text the user sees now
			auto result = Features::Rename::PrepareAt(*session, ExtractUri(params), ExtractPosition(params));
			SendResponse(id, Protocol::LspJson::PrepareRename(result));
		}

		void LspServer::HandleFormatting(const Json::Value& id, const Json::Value& params)
		{
			std::string uri = ExtractUri(params);
			auto session = SnapshotForRequest(); // format the document as the client last sent it

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
		auto session = SnapshotForRequest();

		Core::Vector<Features::CodeAction> actions; // empty when there is no build yet or both features are off
		if (session && session->IsCompiled())
			actions = Features::CodeActions::ForDocument(*session, uri);

		SendResponse(id, Protocol::LspJson::CodeActions(actions, uri));
	}

	void LspServer::HandleFoldingRanges(const Json::Value& id, const Json::Value& params)
	{
		std::string uri = ExtractUri(params);
		auto session = GetSnapshot(); // non-blocking like semantic tokens: Monaco refetches on content change and every diagnostics publish

		Core::Vector<Features::FoldingRange> ranges;
		if (session)
			ranges = Features::FoldingRanges::ForDocument(*session, uri); // empty for files that failed to parse

		SendResponse(id, Protocol::LspJson::FoldingRanges(ranges));
	}
} // namespace C4LSP::Server
