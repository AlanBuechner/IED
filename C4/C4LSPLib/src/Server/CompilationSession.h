#pragma once
#include "Core/Core.h"
#include "Compiler/CompilerInstance.h"
#include "Protocol/LspTypes.h" // Protocol::Position / Protocol::Range used by the coordinate helpers below
#include "Server/SemanticResolver.h" // owned by value (unique_ptr) - the complete type is needed at destruction
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace C4LSP::Server
{
	// Owns a CompilerInstance over the project plus the open documents whose in-memory text
	// overrides disk. Features query it for ASTs, symbols and diagnostics; Recompile() rebuilds
	// the compiler whenever an open document changes.
	class CompilationSession
	{
	public:
		CompilationSession();

		void SetRootFolder(const std::string& root);
		const std::string& GetRootFolder() const { return m_RootFolder; }

		// Build configuration the client's project generator derived from the real compiler args.
		// includeDirs mirror `-I` (macro expansion + interop header resolution only, matching the
		// compiler: they are NOT registered as compilation units); defines seed every file's macro
		// table before preprocessing, like the compiler's predefined macros.
		void SetIncludePaths(const std::vector<std::string>& paths) { m_IncludePaths = paths; }
		const std::vector<std::string>& GetIncludePaths() const { return m_IncludePaths; }
		void SetDefines(std::map<std::string, std::string> defines) { m_Defines = std::move(defines); }
		// The processor the build compiles for (D-62): its SIMD_* macros and which intrinsics are allowed.
		void SetTarget(const Scripting::CpuTarget& target) { m_Target = target; }
		const std::map<std::string, std::string>& GetDefines() const { return m_Defines; }

		// The default library the program's includes resolve against, as in C4.exe's install: the client's
		// c4Home option, else Toolchain::Bedrock(). Only the files the program includes are compiled.
		void SetLibraryFolder(const std::string& folder) { m_LibraryFolder = folder; }

		// The build description C4.exe gets (docs/language/15-projects.md): when set and readable, its projects are
		// what is compiled, each with its own defines and boundary.
		void SetProjectsFile(const std::string& file) { m_ProjectsFile = file; }
		const std::string& GetLibraryFolder() const { return m_LibraryFolder; }

		// Register a source from memory by display name (tests, or files outside the scanned folder).
		void RegisterInMemorySource(const std::string& displayName, const std::string& text);

		// Record an open document's editor contents; the next Recompile() uses this over disk.
		void SetOpenDocument(const std::string& uri, const std::string& text);
		void RemoveOpenDocument(const std::string& uri);

		bool Recompile();

		Scripting::SourceManager& GetSourceManager() { return m_Compiler->GetSourceManager(); }
		const Scripting::SourceManager& GetSourceManager() const { return m_Compiler->GetSourceManager(); }
		Scripting::CompilerInstance& GetCompiler() { return *m_Compiler; }
		const Scripting::CompilerInstance& GetCompiler() const { return *m_Compiler; }

		// The shared on-demand resolver bound to the current program model. Rebuilt (and its caches
		// dropped) on every Recompile(), so features always resolve against the freshest model and can
		// never observe stale node facts after an edit. It drives the model's own query verbs, so LSP
		// semantic facts are exactly what the compiler resolves - the compiler is the source of truth.
		SemanticResolver& GetResolver();
		const SemanticResolver& GetResolver() const;

		// Body-level (HIR lowering) diagnostics from the SAME pass C4.exe runs in CodeGenDriver::Lower:
		// parse + program-model errors come from GetAllDiagnostics(), and these are the type errors a
		// function BODY produces. Together they are the compiler's complete error output for the project,
		// so the LSP reports exactly what the build would - no separate/duplicated error logic in the LSP.
		const Core::Vector<Scripting::CompilationError>& GetLoweringDiagnostics() const { return m_LoweringErrors; }

		std::string UriForSourceId(uint32 sourceId) const;
		uint32 FindSourceIdByUri(const std::string& uri) const; // 0 when the file is unknown
		bool IsCompiled() const { return m_Compiled; }

		// The text the editor currently shows for a document: unsaved contents, else an
		// in-memory source, else the on-disk file. Features that re-lex (semantic tokens) must
		// use this rather than the compiler's effective text, which may be macro-expanded or
		// have CBind bindings appended.
		std::string GetEditorText(const std::string& uri) const;

		// CBind appends generated bindings to interop files' text before parsing, so the user's lines keep
		// their numbers; a location inside the bindings maps to the directive that produced them.
		Protocol::Range ToEditorRange(uint32 sourceId, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end) const;
		Protocol::Location ToEditorLocation(const Scripting::SourceLocation& start, const Scripting::SourceLocation& end) const;

	private:
		// Display name a document URI maps to (root-relative, or just the file name).
		std::string DisplayNameForUri(const std::string& uri) const;
		void ApplyOpenDocumentOverrides();
		// The described projects' sources; false when there is no usable description.
		bool RegisterProjects();
		// Macro expansion + CBind interop bindings before parsing (see .cpp for why).
		void RunPreprocessPhase();
		// Root folders the preprocess passes run over: the project root plus any -I dirs that exist.
		std::vector<std::string> BuildPreprocessRoots() const;
		// Lower every complete function body through the compiler's OWN HIR lowering (LowerFunction),
		// exactly as CodeGenDriver::Lower does, to collect the body-level type diagnostics. No LLVM is
		// emitted - only the diagnostics matter here. Gated on a valid model, mirroring C4.exe which
		// lowers only after a clean front end.
		void RunLoweringPass();

		std::unique_ptr<Scripting::CompilerInstance> m_Compiler;
		std::unique_ptr<SemanticResolver> m_Resolver; // rebuilt on Recompile() to track the current model
		Core::Vector<Scripting::CompilationError> m_LoweringErrors; // body-level (HIR) diagnostics, see GetLoweringDiagnostics
		std::string m_RootFolder;
		std::vector<std::string> m_IncludePaths; // -I dirs from the client's build config (see SetIncludePaths)
		std::string m_LibraryFolder;             // the default library (see SetLibraryFolder)
		std::string m_ProjectsFile;              // the build description (see SetProjectsFile)
		std::map<std::string, std::string> m_Defines;  // predefined macros for every preprocessed file
		Scripting::CpuTarget m_Target;                  // the build's processor (see SetTarget)
		std::map<std::string, std::string> m_InMemorySources; // display name -> text
		std::map<std::string, std::string> m_OpenDocuments;   // uri -> editor text
		std::map<uint32, uint32> m_BindingStartLine;          // sourceId -> 1-based effective line where the bindings start
		std::map<uint32, uint32> m_BindingDirectiveLine;      // sourceId -> 1-based line of the extern include directive
		bool m_Compiled = false;
	};
} // namespace C4LSP::Server
