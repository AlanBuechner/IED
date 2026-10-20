#include "Server/LspServer.h"
#include "Protocol/LspJson.h"
#include "Json/JsonWriter.h"
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
#include "Features/Tidy.h"

namespace C4LSP::Server
{
	LspServer::LspServer(OutputSink send) : m_Send(std::move(send)), m_Session(std::make_unique<CompilationSession>()) {}

	std::shared_ptr<CompilationSession> LspServer::GetSnapshot() const
	{
		std::lock_guard<std::mutex> lock(m_SessionMutex);
		return m_Session;
	}

	std::shared_ptr<CompilationSession> LspServer::SnapshotForRequest()
	{
		// Wait for pending work (bounded), then snapshot. Every read-only feature answers from this:
		// didChange is applied to m_Documents and recompiled on the worker thread, so answering from
		// GetSnapshot() before that swap-in would make the feature operate on stale text (e.g. completion
		// after typing "x." returns globals instead of x's members). The wait stays bounded because on a
		// very large root one recompile can outlive any sane client request timeout; past the deadline we
		// still answer from whatever snapshot exists. The editor's LSP request timeout is 15s, so 10s stays safely
		// below it. Semantic tokens deliberately do NOT use this (they have a refresh channel: the client
		// refetches on every publishDiagnostics), see HandleSemanticTokens.
		WaitForPendingWork(std::chrono::milliseconds(10'000)); // best-effort; false just means "answer now"
		return GetSnapshot();
	}

	LspServer::~LspServer()
	{
		// Stop the worker before members it references (m_Session, m_Documents) are destroyed.
		{
			std::lock_guard<std::mutex> lock(m_WorkMutex);
			m_WorkShutdown = true;
			m_Dirty = false;
		}
		m_WorkCv.notify_all();
		if (m_Worker.joinable())
			m_Worker.join();
	}

	void LspServer::PostRecompile()
	{
		std::lock_guard<std::mutex> lock(m_WorkMutex);
		m_Dirty = true;
		if (!m_WorkerStarted)
		{
			m_WorkerStarted = true;
			m_Worker = std::thread([this] { WorkerLoop(); });
		}
		m_WorkCv.notify_all();
	}

	void LspServer::WorkerLoop()
	{
		while (true)
		{
			bool dirty;
			{
				std::unique_lock<std::mutex> lock(m_WorkMutex);
				m_WorkCv.wait(lock, [this] { return m_Dirty || m_WorkShutdown; });
				dirty = m_Dirty;
				if (m_WorkShutdown && !dirty)
					return;
				m_Dirty = false;
				m_Working = true;
			}

			RunRecompileAndPublish();

			{
				std::lock_guard<std::mutex> lock(m_WorkMutex);
				m_Working = false;
				if (m_WorkShutdown)
					return;
			}
			m_WorkCv.notify_all();
		}
	}

	void LspServer::WaitForCurrentWork()
	{
		std::unique_lock<std::mutex> lock(m_WorkMutex);
		// Wait until there is neither an in-flight recompile nor one queued. Checking only !m_Working
		// would let a request slip through the gap between two cycles and read a stale AST.
		m_WorkCv.wait(lock, [this] { return !m_Working && !m_Dirty; });
	}

	bool LspServer::WaitForPendingWork(std::chrono::milliseconds timeout)
	{
		std::unique_lock<std::mutex> lock(m_WorkMutex);
		return m_WorkCv.wait_for(lock, timeout, [this] { return !m_Working && !m_Dirty; });
	}

	void LspServer::RunRecompileAndPublish()
	{
		std::vector<std::pair<std::string, std::string>> docs;
		{
			std::lock_guard<std::mutex> lock(m_DocMutex);
			m_Documents.ForEachDocument([&](const std::string& uri, const std::string& text) {
				docs.emplace_back(uri, text);
			});
		}

		// Build into a FRESH session so feature requests keep answering against the previous,
		// still-valid snapshot while this (potentially long) recompile runs. Swap it in only when done.
		auto fresh = std::make_unique<CompilationSession>();
		if (!m_RootUri.empty())
			fresh->SetRootFolder(Protocol::DocumentUri::FromUri(m_RootUri));
		fresh->SetIncludePaths(m_IncludePaths); // client build config (initialize) survives every recompile
		fresh->SetDefines(m_Defines);
		fresh->SetTarget(m_Target);
		if (!m_LibraryFolder.empty())
			fresh->SetLibraryFolder(m_LibraryFolder);
		fresh->SetProjectsFile(m_ProjectsFile);
		for (const auto& [uri, text] : docs)
			fresh->SetOpenDocument(uri, text);
		fresh->Recompile();

		std::shared_ptr<CompilationSession> published = std::move(fresh);
		{
			std::lock_guard<std::mutex> lock(m_SessionMutex);
			m_Session = published; // atomically swap the snapshot in
		}

		for (const auto& [uri, text] : docs)
		{
			auto diagnostics = Features::Diagnostics::ForDocument(*published, uri);
			// C4 Tidy appends its non-error code-quality findings to the same publish so the editor's
			// squiggle list shows compiler errors and tidy warnings together (VS2022-style). With the
			// feature disabled this returns empty and nothing changes.
			for (const auto& tidyDiag : Features::Tidy::ForDocument(*published, uri))
				diagnostics.Push(tidyDiag);
			Json::Value params = Json::Value::MakeObject();
			params.SetMember("uri", Json::Value::String(Core::String(uri)));
			params.SetMember("diagnostics", Protocol::LspJson::DiagnosticsArray(diagnostics));
			SendNotification("textDocument/publishDiagnostics", params);
		}
	}

	void LspServer::HandleMessage(const Json::Value& message)
	{
		if (!message.HasMember("method"))
			return; // a response to a server-initiated request; the server issues none
		if (message.HasMember("id"))
			HandleRequest(message);
		else
			HandleNotification(message);
	}

	void LspServer::HandleRequest(const Json::Value& message)
	{
		const std::string method = message.Member("method").GetString();
		int64 id = message.Member("id").GetInt(-1);
		// params must OWN its copy: Value::Member returns by value, so binding a const ref here would dangle.
		Json::Value params = message.HasMember("params") ? message.Member("params") : Json::Value::Null();

		if (method == "initialize")
			HandleInitialize(id, params);
		else if (method == "shutdown")
			HandleShutdown(id);
		else if (!m_Initialized)
			UnknownMethod(id, method);
		else
		{
			// Feature requests answer from the last-completed snapshot WITHOUT waiting for an
			// in-flight recompile. A recompile can outlive a client's request timeout (CBind interop),
			// so blocking here would make every F12/hover time out as "no implementation found".
			if (method == "textDocument/hover")
				HandleHover(id, params);
			else if (method == "textDocument/definition")
				HandleDefinition(id, params);
			else if (method == "textDocument/references")
				HandleReferences(id, params);
			else if (method == "textDocument/documentHighlight")
				HandleDocumentHighlight(id, params);
			else if (method == "textDocument/completion")
				HandleCompletion(id, params);
		else if (method == "textDocument/documentSymbol")
			HandleDocumentSymbol(id, params);
		else if (method == "workspace/symbol")
			HandleWorkspaceSymbols(id, params);
			else if (method == "textDocument/semanticTokens/full")
				HandleSemanticTokens(id, params);
			else if (method == "textDocument/signatureHelp")
				HandleSignatureHelp(id, params);
			else if (method == "textDocument/typeDefinition")
				HandleTypeDefinition(id, params);
			else if (method == "textDocument/declaration")
				HandleDeclaration(id, params);
			else if (method == "textDocument/implementation")
				HandleImplementation(id, params);
			else if (method == "textDocument/rename")
				HandleRename(id, params);
			else if (method == "textDocument/prepareRename")
				HandlePrepareRename(id, params);
			else if (method == "textDocument/formatting" || method == "textDocument/rangeFormatting")
				HandleFormatting(id, params);
			else if (method == "textDocument/codeAction")
				HandleCodeAction(id, params);
			else if (method == "textDocument/foldingRange")
				HandleFoldingRanges(id, params);
			else
				UnknownMethod(id, method);
		}
	}

	void LspServer::HandleNotification(const Json::Value& message)
	{
		const std::string method = message.Member("method").GetString();
		Json::Value params = message.HasMember("params") ? message.Member("params") : Json::Value::Null(); // owned copy (see HandleRequest)

		if (method == "initialized")
			HandleInitialized();
		else if (method == "exit")
		{
			m_ShouldExit = true;
			return;
		}
		else if (!m_Initialized)
			return; // ignore text-document notifications before initialize completes

		if (method == "textDocument/didOpen")
			HandleDidOpen(params);
		else if (method == "textDocument/didChange")
			HandleDidChange(params);
		else if (method == "textDocument/didClose")
			HandleDidClose(params);
		else if (method == "workspace/didChangeConfiguration")
			HandleDidChangeConfiguration(params);
		// didSave and other notifications require no server action.
	}

	void LspServer::UnknownMethod(int64 id, const std::string& method)
	{
		SendError(id, -32601, "Method not found: " + method);
	}

	std::string LspServer::ExtractUri(const Json::Value& params) const
	{
		return params.Member("textDocument").Member("uri").GetString();
	}

	Protocol::Position LspServer::ExtractPosition(const Json::Value& params) const
	{
		const Json::Value& pos = params.Member("position");
		Protocol::Position position;
		position.line = static_cast<uint32>(pos.Member("line").GetInt(0));
		position.character = static_cast<uint32>(pos.Member("character").GetInt(0));
		return position;
	}

	void LspServer::SendResponse(int64 id, const Json::Value& result)
	{
		Json::Value message = Json::Value::MakeObject();
		message.SetMember("jsonrpc", Json::Value::String(Core::String("2.0")));
		message.SetMember("id", Json::Value::Int(id));
		message.SetMember("result", result);
		std::lock_guard<std::mutex> lock(m_SendMutex);
		m_Send(Json::Writer::Encode(message));
	}

	void LspServer::SendError(int64 id, int code, const std::string& message)
	{
		Json::Value error = Json::Value::MakeObject();
		error.SetMember("code", Json::Value::Int(code));
		error.SetMember("message", Json::Value::String(Core::String(message)));

		Json::Value response = Json::Value::MakeObject();
		response.SetMember("jsonrpc", Json::Value::String(Core::String("2.0")));
		response.SetMember("id", Json::Value::Int(id));
		response.SetMember("error", error);
		std::lock_guard<std::mutex> lock(m_SendMutex);
		m_Send(Json::Writer::Encode(response));
	}

	void LspServer::SendNotification(const std::string& method, const Json::Value& params)
	{
		Json::Value message = Json::Value::MakeObject();
		message.SetMember("jsonrpc", Json::Value::String(Core::String("2.0")));
		message.SetMember("method", Json::Value::String(Core::String(method)));
		message.SetMember("params", params);
		std::lock_guard<std::mutex> lock(m_SendMutex);
		m_Send(Json::Writer::Encode(message));
	}
} // namespace C4LSP::Server
