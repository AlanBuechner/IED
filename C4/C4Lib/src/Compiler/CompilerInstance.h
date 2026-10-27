#pragma once
#include "Macro/MacroPass.h"
#include "SourceManager/SourceManager.h"
#include "CompilationUnit.h"
#include "CompilerMessageQueue.h"
#include "Parser/Nodes/Declarations/IncludeDeclarationNode.h"
#include "Model/ProgramModel.h"
#include "Core/Core.h"
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace Scripting
{
	/// The front end: owns the state of a compilation - which sources are registered, their
	/// parsed units, and the single program-wide model built from them. The CLI driver (src/Source.cpp)
	/// orchestrates CompileAll() + AnalyzeProgram(); an LSP host can stop there and serve semantic
	/// information without any backend. There is no HIR here: body lowering is a separate phase that
	/// queries the model on demand (doc 06).
	class CompilerInstance
	{
	public:
		CompilerInstance();
		~CompilerInstance();

		CompilerInstance(const CompilerInstance&) = delete;
		CompilerInstance& operator=(const CompilerInstance&) = delete;

		// registration - delegates to SourceManager
		SourceManager& GetSourceManager() { return m_SourceManager; }
		const SourceManager& GetSourceManager() const { return m_SourceManager; }
		uint32 RegisterFile(const fs::path& path) { return m_SourceManager.RegisterFile(path); }
		uint32 RegisterInMemory(const fs::path& name, const std::string& sourceText) { return m_SourceManager.RegisterInMemory(name, sourceText); }
		uint32 RegisterFolder(const fs::path& folder, FolderScan scan = FolderScan::Recursive) { return m_SourceManager.RegisterFolder(folder, scan); }
		uint32 RegisterIncludedLibraryFiles(const fs::path& root, const std::vector<fs::path>& opened = {}) { return m_SourceManager.RegisterIncludedLibraryFiles(root, opened); }

		// compilation pipeline
		CompilerMessageQueue& GetMessageQueue() { return m_MessageQueue; }
		const CompilerMessageQueue& GetMessageQueue() const { return m_MessageQueue; }
		Ref<CompilationUnit> GetCompilationUnit(uint32 sourceId);

		/// The set of units whose file-scope symbols are visible FROM `sourceId`: the unit itself
		/// plus its full transitive include closure. This is the visibility domain the model's
		/// query API enforces at symbol-request time (doc 09).
		Core::Vector<uint32> VisibleFrom(uint32 sourceId) const;

		uint32 SourceCount() const { return m_SourceManager.Count(); }
		uint32 UnitCount() const { return static_cast<uint32>(m_CompilationUnits.size()); }
		uint32 GetMaxThreads() const { return m_MaxThreads; }
		/// How many files parse at once (at least one); every processor by default. A process that compiles again and
		/// again (the language server) keeps its memory with one: parses on many threads at once leave the heap holding
		/// several times what the program needs, more after each compile (docs/tools/03 "Memory").
		void SetMaxThreads(uint32 threads) { m_MaxThreads = threads > 0 ? threads : 1; }
		Core::Vector<CompilationError> GetAllErrors() const;
		Core::Vector<CompilationError> GetAllDiagnostics() const; // includes warnings
		/// The macro pass's errors and warnings, placed at their files' sources; GetAllDiagnostics lists them first.
		void AddMacroDiagnostics(const std::vector<MacroPass::FileResult>& results);
		void PrintAllErrors() const;

		// session stats (consumed by the diagnostic footer)
		uint32 GetFilesCompleted() const { return m_FilesCompleted; }

		/// Record a named pipeline phase's wall-clock duration (seconds). The driver measures each
		/// phase around the CompilerInstance call it makes, so the instance stays free of timing code.
		void AddPhaseTime(const char* name, double seconds) { m_PhaseTimes.Push({ name, seconds }); }
		const Core::Vector<std::pair<const char*, double>>& GetPhaseTimes() const { return m_PhaseTimes; }
		double GetPhaseTotalSeconds() const
		{
			double total = 0.0;
			for (const auto& p : m_PhaseTimes) total += p.second;
			return total;
		}

		// ------------------------------------------------------------------
		// Compilation pipeline (front end). Parse every registered source into an AST, then build
		// the single program-wide model. After AnalyzeProgram() every unit shares one ProgramModel -
		// the only object that owns type/symbol/constant facts (doc 03).
		// ------------------------------------------------------------------

		/// Parse every registered source file into an AST, resolving include dependencies in
		/// parallel. Does not build the model - call AnalyzeProgram() for that.
		void CompileAll();

		/// Build the single program-wide model: one ModelBuilder over all units' own declarations.
		/// Must run after CompileAll(). Re-runnable (tests rebuild in place).
		bool AnalyzeProgram();

		/// Splice every unit's includes into its root AST, for what walks a document's tree to a
		/// declaration in a header it includes (LSP navigation, --pretty-print). The model and the
		/// backend read each unit's own declarations and never need it.
		void MergeIncludes();

		// ------------------------------------------------------------------
		// The program model (docs/compiler/02-program-model.md). ONE ModelBuilder resolves ALL units' own
		// declarations into a single scope tree, with include-based visibility enforced at lookup
		/// time instead of by merging each unit's closure. Each top-level declaration is owned by
		/// exactly one unit (the file where it is written), so analyzing the union of every reachable
		// unit's OWN children yields each definition once - no merge, no per-unit re-analysis.
		// ------------------------------------------------------------------

		/// Build the program-wide model from every reachable unit's own top-level declarations.
		/// "Reachable" = an entry file or in an entry's transitive include closure. Units with parse
		/// errors are skipped (their parse diagnostics still surface via GetAllDiagnostics()).
		bool BuildProgramModel();
		/// A script compile (docs/runtime/04): the host's symbols, so the functions the host defines count as defined.
		void SetHostSymbols(const std::unordered_set<std::string>* symbols) { m_ProgramModel.Symbols()->SetHostSymbols(symbols); }
		/// The processor the program is compiled for (D-62).
		void SetTarget(const CpuTarget& target) { m_ProgramModel.SetTarget(target); }

		/// The program-wide symbol table; null until the model has built.
		SymbolTable* GetProgramSymbolTable() const { return m_ProgramModel.IsValid() ? m_ProgramModel.Symbols() : nullptr; }

		/// Diagnostics reported while building the program model (e.g. a cross-file redefinition).
		const Core::Vector<CompilationError>& GetProgramModelErrors() const { return m_ProgramModelErrorTracker.GetErrors(); }

		/// The program model: the ONE object every phase holds and queries (doc 03/04). It owns all
		/// type/symbol/constant/template facts; nothing else is handed out. Valid after AnalyzeProgram.
		const ProgramModel& GetProgramModel() const { return m_ProgramModel; }

		/// Non-const model reference for LOWERING phases (doc 06): pushing scopes and registering
		/// locals mutates the scope chain, so a lowerer holds this. Query-only consumers (LSP/DAP)
		/// use the const GetProgramModel().
		ProgramModel& GetProgramModelForLowering() { return m_ProgramModel; }

		/// Whether any entry file defines a lowercase `main` (the real entry point). Lets the driver
		/// distinguish "no user main" from a program that intentionally has none.
		bool HasUserMain() const { return m_HasUserMain; }

		/// True when `displayName` ends with `.cf` - only entry files are lowered by downstream phases.
		static bool IsEntryFile(const std::string& displayName);

	private:
		// --- Parse scheduler (CompileAll) ---------------------------------------
		uint32 EnqueueAllSources();
		void DispatchAvailableUnits();
		bool NoProgressPossible() const;
		void HandleUnitFinished(uint32 sourceId);
		void WakeBlockedDependents(uint32 sourceId, const Ref<CompilationUnit>& finishedUnit);
		void ResumeBlockedUnit(uint32 blockedId, const Ref<CompilationUnit>& dependency);
		void HandleIncludeRequest(const CompilerMessage& msg);
		void BlockRequesterOnDependency(uint32 requesterId, uint32 targetId);
		bool IncludesTransitively(uint32 from, uint32 target);
		bool ReleaseDeadlockedUnits();
		void PromoteDependencyInQueue(uint32 sourceId);
		void RespondToRequester(uint32 requesterId, ResponseType type, const Core::String& message);

		// --- Diagnostics formatting (PrintAllErrors) ----------------------------
		static const char* SeverityText(ErrorSeverity severity);

		/// Scan entry files for a lowercase global `main` (sets m_HasUserMain). Pure AST walk.
		void DetectUserMain();

		SourceManager m_SourceManager;
		std::unordered_map<uint32, Ref<CompilationUnit>> m_CompilationUnits;

		CompilerMessageQueue m_MessageQueue;

		uint32 m_MaxThreads = 0;
		uint32 m_ActiveThreads = 0;

		// scheduler - queue of pending compilation units and their parse threads
		Core::Vector<uint32> m_ParseQueue;

		// include dependency tracking
		std::unordered_set<uint32> m_CompletedUnits;   // source IDs whose parsing has finished
		std::unordered_set<uint32> m_QueueIds;         // source IDs currently in m_ParseQueue

		// include dependency tracking: targetId -> list of requesterIds blocked on it
		std::unordered_map<uint32, Core::Vector<uint32>> m_BlockedUnits;

		// session-level stats
		uint32 m_FilesCompleted = 0;
		Core::Vector<std::pair<const char*, double>> m_PhaseTimes; // per-phase durations, in order

		bool m_HasUserMain = false;

		// The program model (docs/compiler/02-program-model.md): one ModelBuilder over every unit's own
		// declarations fills it; it OWNS all type/symbol/constant facts and is the single object
		// downstream phases query. Rebuilt in place by BuildProgramModel().
		ProgramModel m_ProgramModel;
		ErrorTracker m_ProgramModelErrorTracker; // diagnostics from the model build
		ErrorTracker m_MacroErrorTracker;        // diagnostics from the macro pass
	};
}
