#pragma once
#include "Server/ProgramOptions.h"
#include "Features/WorkspaceSymbols.h"
#include "Compiler/FileIo.h"
#include "Compiler/Projects.h"
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace C4LSP::Server
{
	class CompilationSession;

	// The project a library program's files are, among the projects queries across programs compare (a name no Nuke
	// project can take).
	inline constexpr const char* kLibraryProject = "(the default library)";

	// The editor's open documents a build takes over the files on disk: (uri, text).
	using EditorDocuments = std::vector<std::pair<std::string, std::string>>;

	// A file's size and time as an analysis read it.
	using DiskStamps = std::vector<std::pair<std::string, Scripting::FileStamp>>;

	// The workspace symbols an analysis found in one file. Programs share a file's (most of Claymore's programs hold the
	// engine, its C bindings and the default library): one copy serves each index it is in.
	struct FileSymbols
	{
		Core::Vector<Features::WorkspaceSymbol> symbols;
	};

	// A program's workspace symbols, kept when its analysis is let go (docs/tools/03 "Workspace symbols"): what a search
	// answers from without analyzing the program again.
	struct SymbolIndex
	{
		std::vector<std::shared_ptr<const FileSymbols>> symbols; // WorkspaceSymbols::All of the analysis, file by file
		DiskStamps files;                                        // what they were read from
	};

	// The files' symbols the indexes hold, each kept once while an index holds it (the worker's alone).
	class SharedFileSymbols
	{
	public:
		// The index of `symbols` (WorkspaceSymbols::All of an analysis), each file's symbols the copy an index has already
		// when they are the same.
		std::vector<std::shared_ptr<const FileSymbols>> Share(Core::Vector<Features::WorkspaceSymbol> symbols);

	private:
		std::map<size_t, std::vector<std::weak_ptr<const FileSymbols>>> m_ByContent; // hash of a file's symbols -> copies
	};

	// One program of the workspace (docs/tools/03 "One server for every program"): its options, the projects its build
	// description names, and its last analysis. The server keeps the analyses of the programs its open documents need
	// and of those a query across programs asked for lately; the others hold none.
	class Program
	{
	public:
		explicit Program(ProgramOptions options);

		const ProgramOptions& Options() const { return m_Options; }

		// Reads the build description again when its file changed since it was read; true when it did (or went), so the
		// program must be analyzed again.
		bool Refresh();

		// Whether a described project's sources hold the file at `path`; for the default library's own program, whether the
		// library holds it.
		bool Owns(const std::string& path) const;
		// The described projects' names.
		std::set<std::string> ProjectNames() const;
		// The projects a declaration in `path` reaches: the project holding it and every project using it; for a file of
		// the default library, every project; for another file no project holds, none.
		std::set<std::string> ProjectsReaching(const std::string& path) const;
		// Whether `path` is a file of the default library the program builds with.
		bool InLibrary(const std::string& path) const;

		// A fresh analysis of the program, with the editor's text of `documents` over their files.
		std::shared_ptr<CompilationSession> Analyze(const std::string& rootFolder, const EditorDocuments& documents) const;

		// Whether the last analysis is of `taken` (uri -> hash of the editor's text of each open document it takes now): it
		// compiled their text as it is, and each document it took that is closed now as its file is on disk. An editor that
		// opens or closes a file without changing it needs no new analysis.
		bool AnalysisIsOf(const std::map<std::string, size_t>& taken) const;

		// --- Kept by the server, under its session lock ---------------------------------------------------------------------
		std::shared_ptr<CompilationSession> session;           // the last analysis; null when none is kept
		std::map<std::string, size_t> analyzedWith;            // uri -> hash of the text of each open document it took
		bool stale = false;                                    // something it read changed since (its description, a file on disk)
		std::chrono::steady_clock::time_point wantedUntil;     // a query across programs keeps its analysis until then
		std::shared_ptr<const SymbolIndex> symbols;            // its last analysis's symbols; null when it had none, or a file changed
		bool symbolsWanted = false;                            // a search waits for its symbols (LspServer::IndexWantedPrograms)

	private:
		void ReadDescription();

		ProgramOptions m_Options;
		std::optional<Scripting::ProjectSet> m_Projects;
		std::string m_Stamp; // the description file's size and time when it was read
		mutable std::map<std::string, bool> m_Owned; // path -> Owns, found once per description (every pass asks for each open document)
	};
} // namespace C4LSP::Server
