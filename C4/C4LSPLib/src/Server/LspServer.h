#pragma once
#include "Server/CompilationSession.h"
#include "Protocol/TextDocumentStore.h"
#include "Json/JsonValue.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace C4LSP::Server
{
	// Owns the compilation session and open documents, dispatches incoming JSON-RPC to the
	// features, and emits responses/notifications via a caller-supplied sink (tests capture it).
	//
	// Recompiles run on a dedicated worker thread and build into a SEPARATE CompilationSession that
	// is swapped in atomically when done (double-buffered). Feature requests read the last-completed
	// snapshot: most wait for pending work with a bounded timeout (SnapshotForRequest) because they have
	// no refresh channel - a stale answer would be wrong for the text on screen. The one exception is
	// semantic tokens, which DO have a refresh channel in the editor's client: its provider wires Monaco's
	// onDidChange to textDocument/publishDiagnostics, so it refetches every time this server publishes
	// (i.e. after each recompile). HandleSemanticTokens therefore answers immediately from the current
	// snapshot; a pre-compile empty answer is corrected by the next publish-triggered fetch within one
	// debounce window, and answering without waiting keeps first paint instant on roots whose initial
	// compile takes minutes (C-bind interop headers).
	class LspServer
	{
	public:
		using OutputSink = std::function<void(const std::string&)>; // receives a fully framed message

		explicit LspServer(OutputSink send);
		~LspServer();

		// Handle one already-parsed JSON-RPC document (request, notification, or response).
		void HandleMessage(const Json::Value& message);

		bool ShouldExit() const { return m_ShouldExit; }
		int ExitCode() const { return m_ShutdownRequested ? 0 : 1; }

		// Block until no recompile is running or queued. Tests use this to make post-didOpen
		// assertions deterministic; feature handlers instead wait via SnapshotForRequest (bounded).
		void WaitForCurrentWork();

	// Same as above but bounded: returns false when the deadline passes with work still pending.
	bool WaitForPendingWork(std::chrono::milliseconds timeout);

	private:
		std::shared_ptr<CompilationSession> GetSnapshot() const; // current built session, atomically
		std::shared_ptr<CompilationSession> SnapshotForRequest(); // waits (bounded) for a fresh build, then snapshots

		void HandleRequest(const Json::Value& message);
		void HandleNotification(const Json::Value& message);
		void UnknownMethod(int64 id, const std::string& method);

		// The client pushes the effective LSP profile settings here whenever they change (plugin setting
		// toggles included). Only known keys act; unknown ones are ignored so a future client option
		// never breaks the server. A changed tidy flag re-runs the publish pipeline on the existing
		// snapshot without recompiling from scratch: diagnostics for every open document are rebuilt
		// with (or without) the tidy findings and republished, which is what makes the settings-menu
		// toggle take effect live instead of at next launch.
		void HandleDidChangeConfiguration(const Json::Value& params);

		void HandleInitialize(int64 id, const Json::Value& params);
		void HandleInitialized();
		void HandleShutdown(int64 id);

		void HandleDidOpen(const Json::Value& params);
		void HandleDidChange(const Json::Value& params);
		void HandleDidClose(const Json::Value& params);
		void PostRecompile();       // enqueue a recompile on the worker (non-blocking)
		void WorkerLoop();          // runs on the worker thread
		void RunRecompileAndPublish();

		void HandleHover(int64 id, const Json::Value& params);
		void HandleDefinition(int64 id, const Json::Value& params);
		void HandleReferences(int64 id, const Json::Value& params);
		void HandleDocumentHighlight(int64 id, const Json::Value& params);
		void HandleCompletion(int64 id, const Json::Value& params);
		void HandleDocumentSymbol(int64 id, const Json::Value& params);
	void HandleWorkspaceSymbols(int64 id, const Json::Value& params);
		void HandleSemanticTokens(int64 id, const Json::Value& params);
		void HandleSignatureHelp(int64 id, const Json::Value& params);
		void HandleTypeDefinition(int64 id, const Json::Value& params);
		void HandleDeclaration(int64 id, const Json::Value& params);
		void HandleImplementation(int64 id, const Json::Value& params);
		void HandleRename(int64 id, const Json::Value& params);
	void HandlePrepareRename(int64 id, const Json::Value& params);
		void HandleFormatting(int64 id, const Json::Value& params); // textDocument/formatting + rangeFormatting
		void HandleCodeAction(int64 id, const Json::Value& params);  // textDocument/codeAction (lightbulb)
		void HandleFoldingRanges(int64 id, const Json::Value& params); // textDocument/foldingRange (gutter triangles)

		void SendResponse(int64 id, const Json::Value& result);
		void SendError(int64 id, int code, const std::string& message);
		void SendNotification(const std::string& method, const Json::Value& params);

		Json::Value BuildTextDocumentSync() const;
		Json::Value BuildTriggerCharacters() const;
		Json::Value BuildSignatureHelpTriggers() const;
		std::string ExtractUri(const Json::Value& params) const;
		Protocol::Position ExtractPosition(const Json::Value& params) const;

		OutputSink m_Send;
		std::shared_ptr<CompilationSession> m_Session; // last-completed snapshot (double-buffered)
		Protocol::TextDocumentStore m_Documents;
		std::string m_RootUri;
		std::vector<std::string> m_IncludePaths;  // from initializationOptions.includePaths (-I dirs)
		std::map<std::string, std::string> m_Defines; // from initializationOptions.defines (predefined macros)
		Scripting::CpuTarget m_Target;                 // from initializationOptions.target (the -march / -m flags, D-62)
		std::string m_LibraryFolder;                   // <initializationOptions.c4Home>/Bedrock; empty: the session's default
		std::string m_ProjectsFile;                    // initializationOptions.projects: the build description
		bool m_Initialized = false;
		bool m_ShutdownRequested = false;
		bool m_ShouldExit = false;

		mutable std::mutex m_SessionMutex; // guards the m_Session snapshot pointer (swap vs. read)
		std::mutex m_DocMutex;     // guards m_Documents (read thread mutates, worker reads)
		std::mutex m_SendMutex;    // serializes output frames so two threads never interleave a message

		std::mutex m_WorkMutex;
		std::condition_variable m_WorkCv;
		std::thread m_Worker;
		bool m_WorkerStarted = false;
		bool m_Working = false;  // a recompile is currently running on the worker
		bool m_Dirty = false;    // a recompile is queued
		bool m_WorkShutdown = false;
	};
} // namespace C4LSP::Server
