#pragma once
#include "Server/CompilationSession.h"
#include "Server/Program.h"
#include "Protocol/TextDocumentStore.h"
#include "Protocol/PositionCodec.h"
#include "Json/JsonValue.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace C4LSP::Server
{
	// Owns the open documents and the analyses of the workspace's programs, dispatches incoming JSON-RPC to the features,
	// and emits responses/notifications via a caller-supplied sink (tests capture it).
	//
	// One server serves every program of a workspace (docs/tools/03 "One server for every program"): the options name the
	// programs (initializationOptions.programs, one per options file Nuke writes; the options' own fields are one more,
	// which also takes every document no other program compiles). Each open document is analyzed in one program that
	// compiles it - as few programs as hold them all are analyzed - and a query about a declaration (references,
	// implementations, rename, incoming calls, subtypes, workspace symbols) asks every other program that reaches it too.
	//
	// Analyses run on a dedicated worker thread and build into SEPARATE CompilationSessions that are swapped in when done
	// (double-buffered). Feature requests read the last-completed snapshot: most wait for pending work with a bounded
	// timeout (SnapshotForRequest) because they have no refresh channel - a stale answer would be wrong for the text on
	// screen. Semantic tokens and folding answer at once: the client fetches them again after each publish.
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

		// Block until no analysis is running or queued. Tests use this to make post-didOpen assertions deterministic;
		// feature handlers instead wait via SnapshotForRequest (bounded).
		void WaitForCurrentWork();

		// Same as above but bounded: returns false when the deadline passes with work still pending.
		bool WaitForPendingWork(std::chrono::milliseconds timeout);

		// Tests: while held, queued analyses wait, as on a program too large to analyze between two requests.
		void HoldAnalysis(bool held);

		// How many programs hold an analysis now (tests: only what the open documents and recent queries need).
		size_t AnalyzedProgramCount() const;
		// How many analyses of programs the worker has made so far, and how many programs hold workspace symbols (tests:
		// what an open, a close or a change on disk costs; what a search can answer from).
		size_t AnalysesMade() const;
		size_t IndexedProgramCount() const;

	private:
		// What a request's answer must have been analyzed from: the editor's text of its own document (an outline), or of
		// every open document its program holds (anything that resolves a name declared elsewhere).
		enum class Freshness { Document, Program };

		// The analysis that answers about `uri`: its script scope's (initializationOptions.scripts), else its program's.
		std::shared_ptr<CompilationSession> GetSnapshot(const std::string& uri) const;
		// The analysis answering about `uri` once it is as current as `freshness` asks; analyses of other programs are not
		// waited for. Bounded: on a program too large to analyze in time, the last analysis answers.
		std::shared_ptr<CompilationSession> SnapshotForRequest(const std::string& uri, Freshness freshness = Freshness::Program);
		// Whether the analysis answering `uri` compiled what `freshness` asks of `open` (uri -> hash of each open document's
		// text); the session lock held.
		bool IsCurrentLocked(const std::string& uri, const std::map<std::string, size_t>& open, Freshness freshness) const;
		// Of `open`, the documents `program` takes as the worker places them: those it holds, and for the options' own
		// program those no program holds (the session lock held).
		std::map<std::string, size_t> TakenByLocked(const Program& program, const std::map<std::string, size_t>& open) const;
		// A request waits for the worker: at most `most`, and never past the request's budget; the analyses are the
		// worker's meanwhile (the query lock released).
		void AwaitWork(std::chrono::milliseconds most);
		// Wakes the requests waiting for an analysis (after one is swapped in).
		void NotifyAnalyzed();
		// The program answering about `uri` (the session lock held): the one its document is analyzed in, else one whose
		// analysis compiled the file, else the options' own.
		std::shared_ptr<Program> ProgramOfLocked(const std::string& uri) const;
		int32 ScopeOfUri(const std::string& uri) const;
		// A scope's open documents compiled as its scripts.
		std::shared_ptr<CompilationSession> BuildScriptSession(size_t scope, const EditorDocuments& docs);

		// --- Across programs ---------------------------------------------------------------------------------------------
		// The analyses of the other programs a query at `uri` asks about the declaration in `declarationUri` too: as few
		// as reach every project it reaches that the program of `uri` lacks; analyzed for it when they were not (waited
		// for, a bounded time).
		std::vector<std::shared_ptr<CompilationSession>> OtherAnalyses(const std::string& uri, const std::string& declarationUri);
		std::vector<std::shared_ptr<CompilationSession>> Consult(const std::vector<std::shared_ptr<Program>>& programs);

		// --- Workspace symbols -------------------------------------------------------------------------------------------
		// What a search of the workspace's symbols reads: every analysis there is, and the symbols an analysis let go of
		// left (docs/tools/03 "Workspace symbols").
		struct SymbolSources
		{
			std::vector<std::shared_ptr<CompilationSession>> analyses;
			std::vector<std::shared_ptr<const SymbolIndex>> indexes;
		};
		// The workspace's own programs a search reads, as few as hold every project (the session lock held).
		std::vector<std::shared_ptr<Program>> SearchedProgramsLocked() const;
		// The sources of a search: a searched program with neither an analysis nor symbols is analyzed for it (waited for,
		// a bounded time).
		SymbolSources WorkspaceSymbolSources();
		// The worker, idle: the symbols of one searched program that has none, from an analysis it lets go at once, so the
		// first search of a large workspace does not analyze every program while it waits.
		void IndexNextProgram();
		// The worker, after a pass: the symbols of each program a search asked for (Program::symbolsWanted).
		void IndexWantedPrograms();
		// `program`'s symbols from an analysis of its files as they are on disk, let go at once.
		void Index(Program& program);

		// --- Programs ----------------------------------------------------------------------------------------------------
		// The programs: `own` (the options' own fields) first, then those `programs` names (initializationOptions.programs),
		// then the default library's own program for each library they build with; a program whose options did not change
		// keeps its analysis.
		void SetPrograms(ProgramOptions own, const Json::Value& programs);
		// For each of `docs` no program holds that lies in a default library's own sources (LibraryFolderAbove), that
		// library's own program (the session lock held).
		void AddLibrariesAboveLocked(const EditorDocuments& docs);
		// The worker's pass over the programs: which program analyzes each document, analyses made again where their
		// documents changed, those no longer needed let go. Returns the documents whose analysis is new.
		std::vector<std::string> AnalyzePrograms(const EditorDocuments& docs);
		// Of `programs`, those a query across programs keeps analyzed now (their indexes).
		std::vector<size_t> WantedNow(const std::vector<std::shared_ptr<Program>>& programs) const;
		// `program` analyzed again with `taken` (the open documents it holds) when they, its description or its options
		// changed since its last analysis; true when it was.
		bool AnalyzeIfChanged(Program& program, const EditorDocuments& taken);
		// A script scope's analysis made again when its documents or its host's interface changed; true when it was.
		bool AnalyzeScopeIfChanged(size_t scope, const EditorDocuments& docs);
		// The script scopes' analyses made again where their documents changed; returns those documents.
		std::vector<std::string> AnalyzeScripts(const EditorDocuments& docs);
		// Analyses neither the open documents nor a recent query need, let go.
		void ReleaseUnneeded();
		// The analyses of which a file changed on disk besides the editor (ChangedOnDisk) made again in a pass; the symbols
		// left of one whose file changed, dropped.
		void NoticeDiskChanges();

		void HandleRequest(const Json::Value& message);
		void HandleNotification(const Json::Value& message);
		void UnknownMethod(const Json::Value& id, const std::string& method);

		// The client pushes the effective LSP profile settings here whenever they change (plugin setting toggles
		// included), and the programs when their options files change. Only known keys act; unknown ones are ignored so a
		// future client option never breaks the server. A changed tidy flag re-runs the publish pipeline on the existing
		// snapshots without analyzing again.
		void HandleDidChangeConfiguration(const Json::Value& params);

		void HandleInitialize(const Json::Value& id, const Json::Value& params);
		void HandleInitialized();
		void HandleShutdown(const Json::Value& id);

		void HandleDidOpen(const Json::Value& params);
		void HandleDidChange(const Json::Value& params);
		void HandleDidClose(const Json::Value& params);
		void PostRecompile();       // enqueue a pass on the worker (non-blocking)
		void WorkerLoop();          // runs on the worker thread
		void RunRecompileAndPublish();
		void Publish(const std::vector<std::string>& uris);

		void HandleHover(const Json::Value& id, const Json::Value& params);
		void HandleDefinition(const Json::Value& id, const Json::Value& params);
		void HandleReferences(const Json::Value& id, const Json::Value& params);
		void HandleDocumentHighlight(const Json::Value& id, const Json::Value& params);
		void HandleCompletion(const Json::Value& id, const Json::Value& params);
		void HandleDocumentSymbol(const Json::Value& id, const Json::Value& params);
		void HandleWorkspaceSymbols(const Json::Value& id, const Json::Value& params);
		void HandleSemanticTokens(const Json::Value& id, const Json::Value& params);
		void HandleSignatureHelp(const Json::Value& id, const Json::Value& params);
		void HandleTypeDefinition(const Json::Value& id, const Json::Value& params);
		void HandleDeclaration(const Json::Value& id, const Json::Value& params);
		void HandleImplementation(const Json::Value& id, const Json::Value& params);
		void HandleRename(const Json::Value& id, const Json::Value& params);
		void HandlePrepareRename(const Json::Value& id, const Json::Value& params);
		void HandleFormatting(const Json::Value& id, const Json::Value& params); // textDocument/formatting + rangeFormatting
		void HandleCodeAction(const Json::Value& id, const Json::Value& params);  // textDocument/codeAction (lightbulb)
		void HandleFoldingRanges(const Json::Value& id, const Json::Value& params); // textDocument/foldingRange (gutter triangles)
		void HandlePrepareCallHierarchy(const Json::Value& id, const Json::Value& params);
		void HandleCalls(const Json::Value& id, const Json::Value& params, bool incoming); // callHierarchy/incomingCalls, outgoingCalls
		void HandlePrepareTypeHierarchy(const Json::Value& id, const Json::Value& params);
		void HandleRelatedTypes(const Json::Value& id, const Json::Value& params, bool supertypes); // typeHierarchy/supertypes, subtypes
		void HandleExpandMacro(const Json::Value& id, const Json::Value& params); // c4/expandMacro: {name, expansion} or null

		void SendResponse(const Json::Value& id, const Json::Value& result);
		void SendError(const Json::Value& id, int code, const std::string& message);
		void SendNotification(const std::string& method, const Json::Value& params);

		Json::Value BuildTextDocumentSync() const;
		Json::Value BuildTriggerCharacters() const;
		Json::Value BuildSignatureHelpTriggers() const;
		std::string ExtractUri(const Json::Value& params) const;
		// A document's text for converting its positions: the open document's, else the file's.
		std::string TextOf(const std::string& uri);
		// A codec for one message, between the compiler's UTF-8 columns and the client's encoding (L1).
		Protocol::PositionCodec Codec();
		Protocol::Position ExtractPosition(const Json::Value& params) const;

		OutputSink m_Send;
		Protocol::TextDocumentStore m_Documents;
		std::string m_RootUri;
		std::vector<std::shared_ptr<Program>> m_Programs;   // [0]: the options' own; then initializationOptions.programs
		std::map<std::string, std::shared_ptr<Program>> m_Analyzing; // uri -> the program its document is analyzed in
		std::shared_ptr<CompilationSession> m_Unanalyzed;   // what answers before any analysis: an empty session
		std::vector<ScriptScope> m_Scripts;            // initializationOptions.scripts: folders of scripts and their hosts
		InterfaceCache m_Interfaces;                   // the scopes' host interfaces (the worker's)
		SharedFileSymbols m_SharedSymbols;             // the files' symbols the programs' indexes share (the worker's)
		std::vector<std::shared_ptr<CompilationSession>> m_ScriptSessions; // per scope, null while none of its documents is open
		std::vector<std::map<std::string, size_t>> m_ScriptInputs;         // per scope, the documents its session took (the worker's)
		std::vector<std::shared_ptr<const LoadedInterface>> m_ScriptHosts; // per scope, the interface its session took (the worker's)
		Protocol::PositionEncoding m_Encoding = Protocol::PositionEncoding::Utf16; // negotiated in initialize
		std::string m_RequestUri;                      // the document of the request being answered
		bool m_Initialized = false;
		bool m_ShutdownRequested = false;
		bool m_ShouldExit = false;

		mutable std::mutex m_SessionMutex; // guards m_Programs, the programs' analyses, m_Analyzing and m_ScriptSessions
		std::mutex m_QueryMutex;   // held while features read analyses: by a request (but while it waits) and by Publish
		std::chrono::steady_clock::time_point m_RequestStart; // when the request being answered arrived
		std::mutex m_DocMutex;     // guards m_Documents (read thread mutates, worker reads)
		std::mutex m_SendMutex;    // serializes output frames so two threads never interleave a message

		std::mutex m_WorkMutex;
		std::condition_variable m_WorkCv;
		std::thread m_Worker;
		bool m_WorkerStarted = false;
		bool m_Working = false;  // a pass is currently running on the worker
		bool m_Dirty = false;    // a pass is queued
		bool m_AnalysisHeld = false;   // HoldAnalysis
		std::atomic<size_t> m_AnalysesMade{ 0 }; // AnalysesMade
		bool m_RepublishAll = false; // the next pass publishes every open document (a setting changed what they show)
		bool m_WorkShutdown = false;
	};
} // namespace C4LSP::Server
