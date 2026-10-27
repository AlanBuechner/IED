#include "Server/LspServer.h"
#include "Server/ProgramPlan.h"
#include "Protocol/LspJson.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <chrono>
#include <set>
#include "Features/Diagnostics.h"
#include "Features/Tidy.h"

namespace C4LSP::Server
{
	namespace
	{
		// How long a query across programs keeps the analyses it asked for, and how long it waits for them.
		constexpr auto kConsultedKept = std::chrono::minutes(3);
		constexpr auto kConsultWait = std::chrono::seconds(25);
		// How often the worker looks for analyses it may let go while nothing changes.
		constexpr auto kReleaseEvery = std::chrono::seconds(30);

		std::string PathOfUri(const std::string& uri) { return Protocol::DocumentUri::FromUri(uri); }

		// uri -> hash of its text: what an analysis took from the editor.
		std::map<std::string, size_t> InputsOf(const EditorDocuments& docs)
		{
			std::map<std::string, size_t> inputs;
			for (const auto& [uri, text] : docs) inputs[uri] = std::hash<std::string>{}(text);
			return inputs;
		}
	}

	LspServer::LspServer(OutputSink send) : m_Send(std::move(send)), m_Unanalyzed(std::make_shared<CompilationSession>())
	{
		m_Programs.push_back(std::make_shared<Program>(ProgramOptions{}));
	}

	std::shared_ptr<Program> LspServer::ProgramOfLocked(const std::string& uri) const
	{
		auto analyzing = m_Analyzing.find(uri);
		if (analyzing != m_Analyzing.end() && analyzing->second->session) return analyzing->second;
		const std::string path = PathOfUri(uri);
		for (const auto& program : m_Programs)
			if (program->session && program->Owns(path)) return program;
		for (const auto& program : m_Programs)
			if (program->session && program->session->FindSourceIdByUri(uri) != 0) return program;
		return m_Programs.front();
	}

	std::shared_ptr<CompilationSession> LspServer::GetSnapshot(const std::string& uri) const
	{
		const int32 scope = ScopeOfUri(uri);
		std::lock_guard<std::mutex> lock(m_SessionMutex);
		if (scope >= 0 && static_cast<size_t>(scope) < m_ScriptSessions.size() && m_ScriptSessions[scope]) return m_ScriptSessions[scope];
		std::shared_ptr<Program> program = ProgramOfLocked(uri);
		return program->session ? program->session : m_Unanalyzed;
	}

	std::shared_ptr<CompilationSession> LspServer::SnapshotForRequest(const std::string& uri)
	{
		// Wait for pending work (bounded), then snapshot: didChange is applied to m_Documents and analyzed on the worker,
		// so answering before that swap-in would answer for stale text (completion after typing "x." would offer globals
		// instead of x's members). On a very large program one analysis can outlive any client's request timeout, so past
		// the deadline the current snapshot answers. The editor's LSP request timeout is 15s; 10s stays below it.
		WaitForPendingWork(std::chrono::milliseconds(10'000));
		return GetSnapshot(uri);
	}

	int32 LspServer::ScopeOfUri(const std::string& uri) const
	{
		return m_Scripts.empty() ? -1 : ScopeOf(m_Scripts, PathOfUri(uri));
	}

	size_t LspServer::AnalyzedProgramCount() const
	{
		std::lock_guard<std::mutex> lock(m_SessionMutex);
		size_t count = 0;
		for (const auto& program : m_Programs)
			if (program->session) ++count;
		return count;
	}

	std::shared_ptr<CompilationSession> LspServer::BuildScriptSession(size_t scope, const EditorDocuments& docs)
	{
		auto session = std::make_shared<CompilationSession>();
		if (!m_RootUri.empty()) session->SetRootFolder(PathOfUri(m_RootUri));
		session->SetScript(m_Scripts[scope], m_Interfaces.Get(m_Scripts[scope].interfacePath));
		for (const auto& [uri, text] : docs) session->SetOpenDocument(uri, text);
		session->Recompile();
		return session;
	}

	LspServer::~LspServer()
	{
		// Stop the worker before members it references (the programs, m_Documents) are destroyed.
		{
			std::lock_guard<std::mutex> lock(m_WorkMutex);
			m_WorkShutdown = true;
			m_Dirty = false;
		}
		m_WorkCv.notify_all();
		if (m_Worker.joinable())
			m_Worker.join();
	}

	// --- Programs ------------------------------------------------------------------------------------------------------

	void LspServer::SetPrograms(ProgramOptions own, const Json::Value& programs)
	{
		std::vector<ProgramOptions> wanted;
		wanted.push_back(std::move(own));
		for (ProgramOptions& program : ReadPrograms(programs)) wanted.push_back(std::move(program));
		std::lock_guard<std::mutex> lock(m_SessionMutex);
		std::vector<std::shared_ptr<Program>> made;
		for (ProgramOptions& program : wanted)
		{
			std::shared_ptr<Program> kept;
			for (const auto& known : m_Programs)
				if (known->Options() == program) kept = known;
			made.push_back(kept ? kept : std::make_shared<Program>(std::move(program)));
		}
		m_Programs = std::move(made);
		for (auto it = m_Analyzing.begin(); it != m_Analyzing.end();)
			it = std::find(m_Programs.begin(), m_Programs.end(), it->second) == m_Programs.end() ? m_Analyzing.erase(it) : std::next(it);
	}

	std::vector<std::string> LspServer::AnalyzeScripts(const EditorDocuments& docs)
	{
		std::vector<EditorDocuments> scopeDocs(m_Scripts.size());
		for (const auto& doc : docs)
		{
			const int32 scope = ScopeOfUri(doc.first);
			if (scope >= 0) scopeDocs[scope].push_back(doc);
		}
		m_ScriptInputs.resize(m_Scripts.size());
		std::vector<std::string> analyzed;
		for (size_t scope = 0; scope < m_Scripts.size(); ++scope)
		{
			const auto inputs = InputsOf(scopeDocs[scope]);
			if (inputs == m_ScriptInputs[scope] && (inputs.empty() || m_ScriptSessions[scope])) continue;
			std::shared_ptr<CompilationSession> session = scopeDocs[scope].empty() ? nullptr : BuildScriptSession(scope, scopeDocs[scope]);
			{
				std::lock_guard<std::mutex> lock(m_SessionMutex);
				m_ScriptSessions.resize(m_Scripts.size());
				m_ScriptSessions[scope] = session;
			}
			m_ScriptInputs[scope] = inputs;
			for (const auto& [uri, text] : scopeDocs[scope]) analyzed.push_back(uri);
		}
		return analyzed;
	}

	std::vector<std::string> LspServer::AnalyzePrograms(const EditorDocuments& docs)
	{
		std::vector<std::shared_ptr<Program>> programs;
		std::vector<ProgramFacts> facts;
		std::map<std::string, std::shared_ptr<Program>> before;
		{
			std::lock_guard<std::mutex> lock(m_SessionMutex);
			programs = m_Programs;
			before = m_Analyzing;
			for (const auto& program : programs)
			{
				if (program->Refresh()) program->stale = true;
				ProgramFacts known;
				for (const auto& doc : docs) known.owns.push_back(program->Owns(PathOfUri(doc.first)));
				known.analyzed = program->session != nullptr;
				known.projects = program->ProjectNames().size();
				facts.push_back(std::move(known));
			}
		}
		const std::vector<int32> assigned = AssignDocuments(facts, docs.size());

		// Each program analyzes every open document it holds; the options' own also takes those no program holds.
		std::vector<EditorDocuments> taken(programs.size());
		std::map<std::string, std::shared_ptr<Program>> analyzing;
		std::set<size_t> needed;
		for (size_t d = 0; d < docs.size(); ++d)
		{
			for (size_t p = 0; p < programs.size(); ++p)
				if (facts[p].owns[d]) taken[p].push_back(docs[d]);
			const size_t placed = assigned[d] >= 0 ? static_cast<size_t>(assigned[d]) : 0;
			if (assigned[d] < 0) taken[0].push_back(docs[d]);
			analyzing[docs[d].first] = programs[placed];
			needed.insert(placed);
		}
		const auto now = std::chrono::steady_clock::now();
		{
			std::lock_guard<std::mutex> lock(m_SessionMutex);
			for (size_t p = 0; p < programs.size(); ++p)
				if (programs[p]->wantedUntil > now) needed.insert(p);
		}

		std::vector<std::string> analyzed;
		for (size_t p : needed)
		{
			const auto inputs = InputsOf(taken[p]);
			{
				std::lock_guard<std::mutex> lock(m_SessionMutex);
				if (programs[p]->session && !programs[p]->stale && programs[p]->analyzedWith == inputs) continue;
			}
			std::shared_ptr<CompilationSession> fresh = programs[p]->Analyze(m_RootUri.empty() ? "" : PathOfUri(m_RootUri), taken[p]);
			{
				std::lock_guard<std::mutex> lock(m_SessionMutex);
				programs[p]->session = fresh;
				programs[p]->analyzedWith = inputs;
				programs[p]->stale = false;
			}
			for (const auto& [uri, program] : analyzing)
				if (program == programs[p]) analyzed.push_back(uri);
		}
		for (const auto& [uri, program] : analyzing)
		{
			auto was = before.find(uri);
			if (was == before.end() || was->second != program) analyzed.push_back(uri);
		}
		{
			std::lock_guard<std::mutex> lock(m_SessionMutex);
			m_Analyzing = std::move(analyzing);
		}
		ReleaseUnneeded();
		return analyzed;
	}

	void LspServer::ReleaseUnneeded()
	{
		const auto now = std::chrono::steady_clock::now();
		std::lock_guard<std::mutex> lock(m_SessionMutex);
		std::set<Program*> analyzing;
		for (const auto& [uri, program] : m_Analyzing) analyzing.insert(program.get());
		for (const auto& program : m_Programs)
			if (program->session && !analyzing.count(program.get()) && program->wantedUntil <= now)
			{
				program->session.reset();
				program->analyzedWith.clear();
			}
	}

	// --- Across programs -----------------------------------------------------------------------------------------------

	std::vector<std::shared_ptr<CompilationSession>> LspServer::OtherAnalyses(const std::string& uri, const std::string& declarationUri)
	{
		if (ScopeOfUri(uri) >= 0) return {}; // a script is no program's
		const std::string path = PathOfUri(declarationUri);
		std::vector<std::shared_ptr<Program>> consulted;
		{
			std::lock_guard<std::mutex> lock(m_SessionMutex);
			const std::shared_ptr<Program> asked = ProgramOfLocked(uri);
			std::vector<ReachFacts> facts;
			int32 askedIndex = -1;
			for (size_t p = 0; p < m_Programs.size(); ++p)
			{
				facts.push_back({ m_Programs[p]->ProjectsReaching(path), m_Programs[p]->ProjectNames(), m_Programs[p]->session != nullptr });
				if (m_Programs[p] == asked) askedIndex = static_cast<int32>(p);
			}
			for (size_t p : ProgramsToConsult(facts, askedIndex)) consulted.push_back(m_Programs[p]);
		}
		return Consult(consulted);
	}

	std::vector<std::shared_ptr<CompilationSession>> LspServer::AllAnalyses()
	{
		std::vector<std::shared_ptr<Program>> consulted;
		{
			std::lock_guard<std::mutex> lock(m_SessionMutex);
			std::vector<ReachFacts> facts;
			for (const auto& program : m_Programs)
				facts.push_back({ program->ProjectNames(), program->ProjectNames(), program->session != nullptr });
			for (size_t p : ProgramsToConsult(facts, -1)) consulted.push_back(m_Programs[p]);
			for (const auto& program : m_Programs)
				if (program->session && std::find(consulted.begin(), consulted.end(), program) == consulted.end()) consulted.push_back(program);
		}
		return Consult(consulted);
	}

	std::vector<std::shared_ptr<CompilationSession>> LspServer::Consult(const std::vector<std::shared_ptr<Program>>& programs)
	{
		bool missing = false;
		{
			std::lock_guard<std::mutex> lock(m_SessionMutex);
			const auto until = std::chrono::steady_clock::now() + kConsultedKept;
			for (const auto& program : programs)
			{
				program->wantedUntil = std::max(program->wantedUntil, until);
				missing = missing || !program->session || program->stale;
			}
		}
		if (missing)
		{
			PostRecompile();
			WaitForPendingWork(std::chrono::duration_cast<std::chrono::milliseconds>(kConsultWait));
		}
		std::vector<std::shared_ptr<CompilationSession>> sessions;
		std::lock_guard<std::mutex> lock(m_SessionMutex);
		for (const auto& program : programs)
			if (program->session) sessions.push_back(program->session);
		return sessions;
	}

	// --- The worker ----------------------------------------------------------------------------------------------------

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
			{
				std::unique_lock<std::mutex> lock(m_WorkMutex);
				const bool woken = m_WorkCv.wait_for(lock, kReleaseEvery, [this] { return m_Dirty || m_WorkShutdown; });
				if (m_WorkShutdown && !m_Dirty)
					return;
				if (!woken)
				{
					lock.unlock();
					ReleaseUnneeded(); // a query's analyses outlive it a while, then go
					continue;
				}
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
		// Wait until there is neither an in-flight pass nor one queued. Checking only !m_Working would let a request slip
		// through the gap between two passes and read a stale AST.
		m_WorkCv.wait(lock, [this] { return !m_Working && !m_Dirty; });
	}

	bool LspServer::WaitForPendingWork(std::chrono::milliseconds timeout)
	{
		std::unique_lock<std::mutex> lock(m_WorkMutex);
		return m_WorkCv.wait_for(lock, timeout, [this] { return !m_Working && !m_Dirty; });
	}

	void LspServer::RunRecompileAndPublish()
	{
		EditorDocuments docs;
		{
			std::lock_guard<std::mutex> lock(m_DocMutex);
			m_Documents.ForEachDocument([&](const std::string& uri, const std::string& text) { docs.emplace_back(uri, text); });
		}
		// A script scope's documents compile as its scripts, apart from every program (initializationOptions.scripts).
		EditorDocuments programDocs;
		for (const auto& doc : docs)
			if (ScopeOfUri(doc.first) < 0) programDocs.push_back(doc);
		std::vector<std::string> analyzed = AnalyzeScripts(docs);
		for (std::string& uri : AnalyzePrograms(programDocs)) analyzed.push_back(std::move(uri));
		Publish(analyzed);
	}

	void LspServer::Publish(const std::vector<std::string>& uris)
	{
		std::set<std::string> published;
		for (const std::string& uri : uris)
		{
			if (!published.insert(uri).second) continue;
			{
				std::lock_guard<std::mutex> lock(m_DocMutex);
				if (!m_Documents.IsOpen(uri)) continue;
			}
			auto analyzed = GetSnapshot(uri);
			auto diagnostics = Features::Diagnostics::ForDocument(*analyzed, uri);
			// C4 Tidy appends its non-error code-quality findings to the same publish so the editor's squiggle list shows
			// compiler errors and tidy warnings together (VS2022-style). With the feature disabled this returns empty.
			for (const auto& tidyDiag : Features::Tidy::ForDocument(*analyzed, uri))
				diagnostics.Push(tidyDiag);
			Json::Value params = Json::Value::MakeObject();
			params.SetMember("uri", Json::Value::String(Core::String(uri)));
			params.SetMember("diagnostics", Protocol::LspJson::DiagnosticsArray(diagnostics));
			SendNotification("textDocument/publishDiagnostics", params);
		}
	}

	// --- Messages ------------------------------------------------------------------------------------------------------

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
		const Json::Value id = message.Member("id");
		// params must OWN its copy: Value::Member returns by value, so binding a const ref here would dangle.
		Json::Value params = message.HasMember("params") ? message.Member("params") : Json::Value::Null();
		// The request's document is where its positions (and its answer's without a URI) are; a UTF-16 client's columns
		// become the compiler's UTF-8 ones here, and go back in SendResponse.
		m_RequestUri = ExtractUri(params);
		if (m_Encoding == Protocol::PositionEncoding::Utf16 && method != "initialize")
			params = Codec().FromClient(params, m_RequestUri);

		if (method == "initialize")
			HandleInitialize(id, params);
		else if (method == "shutdown")
			HandleShutdown(id);
		else if (!m_Initialized)
			UnknownMethod(id, method);
		else if (method == "textDocument/hover")
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
		else if (method == "textDocument/prepareCallHierarchy")
			HandlePrepareCallHierarchy(id, params);
		else if (method == "callHierarchy/incomingCalls" || method == "callHierarchy/outgoingCalls")
			HandleCalls(id, params, method == "callHierarchy/incomingCalls");
		else if (method == "textDocument/prepareTypeHierarchy")
			HandlePrepareTypeHierarchy(id, params);
		else if (method == "typeHierarchy/supertypes" || method == "typeHierarchy/subtypes")
			HandleRelatedTypes(id, params, method == "typeHierarchy/supertypes");
		else if (method == "c4/expandMacro")
			HandleExpandMacro(id, params);
		else
			UnknownMethod(id, method);
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

	void LspServer::UnknownMethod(const Json::Value& id, const std::string& method)
	{
		SendError(id, -32601, "Method not found: " + method);
	}

	std::string LspServer::TextOf(const std::string& uri)
	{
		{
			std::lock_guard<std::mutex> lock(m_DocMutex);
			if (const std::string* text = m_Documents.GetText(uri))
				return *text;
		}
		return GetSnapshot(uri)->GetEditorText(uri);
	}

	Protocol::PositionCodec LspServer::Codec()
	{
		return Protocol::PositionCodec([this](const std::string& uri) { return TextOf(uri); });
	}

	std::string LspServer::ExtractUri(const Json::Value& params) const
	{
		// A hierarchy request names its document in the item it continues from.
		if (!params.Member("textDocument").Member("uri").IsString() && params.Member("item").Member("uri").IsString())
			return params.Member("item").Member("uri").GetString();
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

	void LspServer::SendResponse(const Json::Value& id, const Json::Value& result)
	{
		Json::Value message = Json::Value::MakeObject();
		message.SetMember("jsonrpc", Json::Value::String(Core::String("2.0")));
		message.SetMember("id", id);
		message.SetMember("result", m_Encoding == Protocol::PositionEncoding::Utf16 ? Codec().ToClient(result, m_RequestUri) : result);
		std::lock_guard<std::mutex> lock(m_SendMutex);
		m_Send(Json::Writer::Encode(message));
	}

	void LspServer::SendError(const Json::Value& id, int code, const std::string& message)
	{
		Json::Value error = Json::Value::MakeObject();
		error.SetMember("code", Json::Value::Int(code));
		error.SetMember("message", Json::Value::String(Core::String(message)));

		Json::Value response = Json::Value::MakeObject();
		response.SetMember("jsonrpc", Json::Value::String(Core::String("2.0")));
		response.SetMember("id", id);
		response.SetMember("error", error);
		std::lock_guard<std::mutex> lock(m_SendMutex);
		m_Send(Json::Writer::Encode(response));
	}

	void LspServer::SendNotification(const std::string& method, const Json::Value& params)
	{
		Json::Value message = Json::Value::MakeObject();
		message.SetMember("jsonrpc", Json::Value::String(Core::String("2.0")));
		message.SetMember("method", Json::Value::String(Core::String(method)));
		message.SetMember("params", m_Encoding == Protocol::PositionEncoding::Utf16 ? Codec().ToClient(params, "") : params);
		std::lock_guard<std::mutex> lock(m_SendMutex);
		m_Send(Json::Writer::Encode(message));
	}
} // namespace C4LSP::Server
