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
	void LspServer::HandleDidChangeConfiguration(const Json::Value& params)
	{
		bool tidyChanged = false;
		const Json::Value settings = params.Member("settings"); // {settings:{...}} per the client's updateSettings
		if (settings.IsObject())
		{
			const Json::Value tidy = settings.Member("tidy");
			if (tidy.IsBool() && Features::Tidy::IsEnabled() != tidy.GetBool())
			{
				Features::Tidy::SetEnabled(tidy.GetBool());
				tidyChanged = true;
			}
			// Code-action features are request-driven (the lightbulb queries on demand), so a live
			// toggle needs no republish - the next textDocument/codeAction simply sees the new flags.
			const Json::Value quickfixes = settings.Member("quickfixes");
			if (quickfixes.IsBool())
				Features::CodeActions::SetQuickfixesEnabled(quickfixes.GetBool());
			const Json::Value createDefinition = settings.Member("createDefinition");
			if (createDefinition.IsBool())
				Features::CodeActions::SetCreateDefinitionEnabled(createDefinition.GetBool());
		}

		// No documents open yet: the flag is stored and the next recompile picks it up. With no work
		// queued, PostRecompile would build an empty session and publish nothing - harmless either way,
		// but only queue when there is actually something to republish.
		if (!tidyChanged)
			return;

		std::vector<std::pair<std::string, std::string>> docs;
		{
			std::lock_guard<std::mutex> lock(m_DocMutex);
			m_Documents.ForEachDocument([&](const std::string& uri, const std::string& text) {
				docs.emplace_back(uri, text);
			});
		}
		if (docs.empty())
			return;

		// Republish from the CURRENT snapshot without a full recompile: RunRecompileAndPublish would
		// rebuild every source just to flip one flag. Rebuild each open document's diagnostics with
		// the new tidy state and send them, exactly like the publish loop does after a real build.
		auto session = GetSnapshot();
		if (!session)
			return;
		for (const auto& [uri, text] : docs)
		{
			auto diagnostics = Features::Diagnostics::ForDocument(*session, uri);
			for (const auto& tidyDiag : Features::Tidy::ForDocument(*session, uri))
				diagnostics.Push(tidyDiag);
			Json::Value params2 = Json::Value::MakeObject();
			params2.SetMember("uri", Json::Value::String(Core::String(uri)));
			params2.SetMember("diagnostics", Protocol::LspJson::DiagnosticsArray(diagnostics));
			SendNotification("textDocument/publishDiagnostics", params2);
		}
	}

	void LspServer::HandleInitialize(const Json::Value& id, const Json::Value& params)
	{
		if (params.HasMember("rootUri"))
			m_RootUri = params.Member("rootUri").GetString();

		// The project generator passes the compiler's include dirs (-I) and defines through
		// initializationOptions so LSP diagnostics match a real build. Malformed entries are ignored:
		// a bad client option must never break the server.
		m_IncludePaths.clear();
		m_Defines.clear();
		m_Target = Scripting::CpuTarget();
		m_LibraryFolder.clear();
		m_ProjectsFile.clear();
		const Json::Value options = params.Member("initializationOptions");
		if (options.IsObject()) {
			// The build description Nuke writes (docs/language/15-projects.md): the same projects the compiler gets.
			const Json::Value projects = options.Member("projects");
			if (projects.IsString())
				m_ProjectsFile = projects.GetString();
			// The C4 install the project builds with (Nuke writes its path): its Bedrock is the default library.
			const Json::Value home = options.Member("c4Home");
			if (home.IsString() && !home.GetString().empty())
				m_LibraryFolder = home.GetString() + "/Bedrock";
			const Json::Value includePaths = options.Member("includePaths");
			for (uint32 i = 0; i < includePaths.Size(); ++i)
				if (includePaths.At(i).IsString() && !includePaths.At(i).GetString().empty())
					m_IncludePaths.push_back(includePaths.At(i).GetString());
			const Json::Value defines = options.Member("defines");
			if (defines.IsObject()) {
				for (const auto& [name, value] : defines.Raw().GetAsDescriptionMap())
					if (value.IsString())
						m_Defines[std::string(name)] = std::string(Json::Value(value).GetString());
			}
			// The processor the build compiles for, as the compiler's -march / -m flags; flags that select none are ignored.
			const Json::Value target = options.Member("target");
			std::vector<std::string> targetFlags;
			for (uint32 i = 0; i < target.Size(); ++i)
				if (target.At(i).IsString()) targetFlags.push_back(target.At(i).GetString());
			std::string targetError;
			if (std::optional<Scripting::CpuTarget> selected = Scripting::CpuTarget::FromFlags(targetFlags, targetError))
				m_Target = *selected;
			// C4 Tidy on/off: the plugin's "feature-tidy" setting is forwarded here by the client at startup.
			// A missing key keeps the server-side default (off); a non-bool value is ignored so a bad
			// client option never breaks initialization.
			const Json::Value tidy = options.Member("tidy");
			if (tidy.IsBool())
				Features::Tidy::SetEnabled(tidy.GetBool());
			// Code-action features: same convention as tidy. A missing key keeps the server-side
			// default (on) so a bare LSP client still gets the lightbulb; non-bool values ignored.
			const Json::Value quickfixes = options.Member("quickfixes");
			if (quickfixes.IsBool())
				Features::CodeActions::SetQuickfixesEnabled(quickfixes.GetBool());
			const Json::Value createDefinition = options.Member("createDefinition");
			if (createDefinition.IsBool())
				Features::CodeActions::SetCreateDefinitionEnabled(createDefinition.GetBool());
		}

		// Columns are UTF-8 bytes in the compiler: a client that takes utf-8 gets them as they are; another gets UTF-16 (L1).
		m_Encoding = Protocol::NegotiatePositionEncoding(params);
		Json::Value capabilities = Json::Value::MakeObject();
		capabilities.SetMember("positionEncoding", Json::Value::String(Core::String(Protocol::PositionEncodingName(m_Encoding))));
		capabilities.SetMember("textDocumentSync", BuildTextDocumentSync());
		capabilities.SetMember("hoverProvider", Json::Value::Bool(true));
		capabilities.SetMember("definitionProvider", Json::Value::Bool(true));
		capabilities.SetMember("referencesProvider", Json::Value::Bool(true));
		capabilities.SetMember("documentHighlightProvider", Json::Value::Bool(true));
		Json::Value completionOptions = Json::Value::MakeObject();
		completionOptions.SetMember("triggerCharacters", BuildTriggerCharacters());
		capabilities.SetMember("completionProvider", completionOptions);
		capabilities.SetMember("documentSymbolProvider", Json::Value::Bool(true));
		capabilities.SetMember("workspaceSymbolProvider", Json::Value::Bool(true));

		// VS-like navigation and editing.
		Json::Value signatureHelpOptions = Json::Value::MakeObject();
		signatureHelpOptions.SetMember("triggerCharacters", BuildSignatureHelpTriggers());
		capabilities.SetMember("signatureHelpProvider", signatureHelpOptions);
		capabilities.SetMember("declarationProvider", Json::Value::Bool(true));
		capabilities.SetMember("typeDefinitionProvider", Json::Value::Bool(true));
		capabilities.SetMember("implementationProvider", Json::Value::Bool(true));
		Json::Value renameOptions = Json::Value::MakeObject();
		renameOptions.SetMember("prepareProvider", Json::Value::Bool(true));
		capabilities.SetMember("renameProvider", renameOptions);

		// Formatting: whitespace-only, lexer-driven (see Features/Formatting.h), so it is safe on
		// files with parse errors. Both document and range formatting are served by the same engine.
		capabilities.SetMember("documentFormattingProvider", Json::Value::Bool(true));
		capabilities.SetMember("documentRangeFormattingProvider", Json::Value::Bool(true));

		// Code actions (the VS2022 lightbulb): quickfixes for the dot-on-pointer error and a
		// "create definition" source action. The client scopes them to markers itself; both feature
		// families are server-side toggleable through plugin settings, so this stays advertised even
		// when disabled - with both off ForDocument returns an empty array (override-only).
		capabilities.SetMember("codeActionProvider", Json::Value::Bool(true));

		// Folding ranges: AST-derived code folds (functions, types, namespaces, braced blocks) plus
		// comment-block folds. VS2022 shows the gutter triangles from this; Monaco's folding model
		// consumes it so Ctrl+Shift+[ / ] and the margin affordances work on C4 sources.
		Json::Value foldingOptions = Json::Value::MakeObject();
		foldingOptions.SetMember("rangeLimit", Json::Value::Int(1000)); // cap for very large files
		capabilities.SetMember("foldingRangeProvider", foldingOptions);

		// Semantic tokens: full-document, with the standard C++-like legend so a client's C++
		// colouring applies to C4 sources.
		Json::Value semanticTokensOptions = Json::Value::MakeObject();
		semanticTokensOptions.SetMember("legend", Protocol::LspJson::SemanticTokensLegend());
		semanticTokensOptions.SetMember("full", Json::Value::Bool(true));
		capabilities.SetMember("semanticTokensProvider", semanticTokensOptions);

		Json::Value result = Json::Value::MakeObject();
		result.SetMember("capabilities", capabilities);
		SendResponse(id, result);
	}

	void LspServer::HandleInitialized()
	{
		m_Initialized = true;
		PostRecompile();
	}

	void LspServer::HandleShutdown(const Json::Value& id)
	{
		m_ShutdownRequested = true;
		SendResponse(id, Json::Value::Null());
	}

	void LspServer::HandleDidOpen(const Json::Value& params)
	{
		const Json::Value& doc = params.Member("textDocument");
		std::lock_guard<std::mutex> lock(m_DocMutex);
		m_Documents.Open(doc.Member("uri").GetString(), doc.Member("text").GetString(),
			static_cast<int>(doc.Member("version").GetInt(0)));
		PostRecompile();
	}

	void LspServer::HandleDidChange(const Json::Value& params)
	{
		const Json::Value& doc = params.Member("textDocument");
		std::string uri = doc.Member("uri").GetString();
		int version = static_cast<int>(doc.Member("version").GetInt(0));

		std::vector<Protocol::TextContentChangeEvent> changes;
		bool fullSync = true;
		const Json::Value& contentChanges = params.Member("contentChanges");
		for (uint32 i = 0; i < contentChanges.Size(); ++i)
		{
			const Json::Value& change = contentChanges.At(i);
			Protocol::TextContentChangeEvent event;
			event.text = change.Member("text").GetString();
			if (change.HasMember("range"))
				fullSync = false; // an incremental edit was sent
			changes.push_back(std::move(event));
		}

		{
			std::lock_guard<std::mutex> lock(m_DocMutex);
			m_Documents.Change(uri, changes, fullSync);
		}
		PostRecompile();
	}

	void LspServer::HandleDidClose(const Json::Value& params)
	{
		std::string uri = params.Member("textDocument").Member("uri").GetString();
		{
			std::lock_guard<std::mutex> lock(m_DocMutex);
			m_Documents.Close(uri);
		}
		PostRecompile();
	}

	Json::Value LspServer::BuildSignatureHelpTriggers() const
	{
		Json::Value array = Json::Value::MakeArray();
		array.Append(Json::Value::String(Core::String("(")));
		array.Append(Json::Value::String(Core::String(",")));
		return array;
	}

	Json::Value LspServer::BuildTextDocumentSync() const
	{
		Json::Value sync = Json::Value::MakeObject();
		sync.SetMember("openClose", Json::Value::Bool(true));
		sync.SetMember("change", Json::Value::Int(1)); // 1 = full text on every change
		sync.SetMember("save", Json::Value::Bool(true));
		return sync;
	}

	Json::Value LspServer::BuildTriggerCharacters() const
	{
		// The character that completes an accessor: `.`, and the `>` of `->` (Completion finds the `-` before it).
		Json::Value array = Json::Value::MakeArray();
		array.Append(Json::Value::String(Core::String(".")));
		array.Append(Json::Value::String(Core::String(">")));
		return array;
	}
} // namespace C4LSP::Server
