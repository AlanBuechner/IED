#include "Macro/PredefinedMacros.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Macro/MacroPass.h"
#include "Interop/InteropPass.h"
#include "Backend/HIR/LoweringDriver.h" // LowerFunction: the compiler's body-level type-checking pass
#include "Compiler/Toolchain.h"
#include "Compiler/ProjectFile.h"
#include "Compiler/SourceMacros.h"
#include <unordered_map>
#include "Protocol/LspTypes.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <cstdio>
#include <chrono>

namespace fs = std::filesystem;

namespace C4LSP::Server
{
	CompilationSession::CompilationSession()
		: m_Compiler(std::make_unique<Scripting::CompilerInstance>()), m_LibraryFolder(Scripting::Toolchain::Bedrock().string()) {}

	SemanticResolver& CompilationSession::GetResolver()
	{
		if (!m_Resolver)
			m_Resolver = std::make_unique<SemanticResolver>(m_Compiler->GetProgramModel());
		return *m_Resolver;
	}

	const SemanticResolver& CompilationSession::GetResolver() const
	{
		return GetResolver();
	}

	void CompilationSession::SetRootFolder(const std::string& root)
	{
		m_RootFolder = root;
		std::replace(m_RootFolder.begin(), m_RootFolder.end(), '\\', '/');
	}

	void CompilationSession::RegisterInMemorySource(const std::string& displayName, const std::string& text)
	{
		m_InMemorySources[displayName] = text;
	}

	void CompilationSession::SetOpenDocument(const std::string& uri, const std::string& text)
	{
		m_OpenDocuments[uri] = text;
	}

	void CompilationSession::RemoveOpenDocument(const std::string& uri)
	{
		m_OpenDocuments.erase(uri);
	}

	bool CompilationSession::Recompile()
	{
		// A fresh instance per compile: the previous ASTs/symbol tables are discarded together,
		// so no feature can observe a stale tree after an edit. (Types are process-lifetime by
		// design, matching the compiler's own memory model.)
		m_Compiler = std::make_unique<Scripting::CompilerInstance>();
		m_Compiler->SetTarget(m_Target);

		if (!RegisterProjects())
		{
			if (!m_RootFolder.empty() && fs::is_directory(m_RootFolder))
				m_Compiler->RegisterFolder(fs::path(m_RootFolder));

			for (const auto& dir : m_IncludePaths) {
				// A real build registers every source under the -I dirs as a compilation unit too
				// (the project generator feeds them to the compiler as inputs), so LSP must do the same:
				// their files resolve against `include` and their errors surface, exactly like the build.
				if (!dir.empty() && fs::is_directory(dir))
					m_Compiler->RegisterFolder(fs::path(dir));
			}
		}

		for (const auto& [name, text] : m_InMemorySources)
			m_Compiler->RegisterInMemory(name, text);

		ApplyOpenDocumentOverrides();
		m_Compiler->RegisterIncludedLibraryFiles(fs::path(m_LibraryFolder)); // as C4.exe does: after the program's own sources
		RunPreprocessPhase();

		m_Compiler->CompileAll();

		// Semantic analysis is a single program-wide pass: it merges each unit's includes into its
		// root AST (for navigation) and runs ONE analyzer over all units' own declarations. The LSP
		// serves diagnostics and semantic information from that one result - the same source of truth
		// the compiler uses - so it can never report errors the build does not produce.
		m_Compiler->AnalyzeProgram();

		// Body-level type checking: run the compiler's OWN HIR lowering over every complete body and
		// collect its diagnostics (RunLoweringPass). Together with GetAllDiagnostics() these are exactly
		// the errors C4.exe reports for the project - parse + program-model + body-lowering. The LSP
		// surfaces all three, computed by the compiler itself, so there is no separate error logic here.
		RunLoweringPass();

		// A fresh resolver bound to the new model: stale node facts from the previous compile are dropped,
		// and any template instantiations registered while lowering above are already visible to it.
		m_Resolver = std::make_unique<SemanticResolver>(m_Compiler->GetProgramModel());

		m_Compiled = true;
		return true;
	}

	bool CompilationSession::RegisterProjects()
	{
		if (m_ProjectsFile.empty()) return false;
		const Scripting::ProjectFileResult read = Scripting::ReadProjectFile(fs::path(m_ProjectsFile));
		Scripting::ProjectSetResult checked = read.error.empty() ? Scripting::ProjectSet::Check(read.list) : Scripting::ProjectSetResult{};
		if (!checked.set)
		{
			std::fprintf(stderr, "C4LSP: the build description is not usable (%s); compiling the workspace folder\n",
				(read.error.empty() ? checked.error : read.error).c_str());
			return false;
		}
		auto& sources = m_Compiler->GetSourceManager();
		sources.SetProjects(std::move(*checked.set));
		for (uint32 p = 0; p < sources.Projects().Count(); ++p)
			for (const fs::path& source : sources.Projects().At(p).sources)
				if (fs::is_directory(source)) m_Compiler->RegisterFolder(source);
				else m_Compiler->RegisterFile(source);
		return true;
	}

	void CompilationSession::RunLoweringPass()
	{
		m_LoweringErrors.Clear();
		if (!m_Compiler->GetProgramModel().IsValid())
			return; // a broken front end already reports its own errors; C4.exe never lowers then either

		// Mirror the compiler's pipeline EXACTLY (src/Source.cpp): HIR lowering is gated on the front
		// end reporting no hard errors (CompilationResult::HasErrors). Lowering a partially-built model
		// re-reports problems the front end already surfaced (e.g. an undefined symbol), which would show
		// two squiggles where C4.exe shows one. Skip lowering whenever any non-warning diagnostic exists,
		// so LSP diagnostics are byte-for-byte what C4.exe reports for the project. A clean front end
		// still lowers, surfacing genuine body-level type errors the model build does not catch.
		for (const auto& err : m_Compiler->GetAllDiagnostics())
			if (err.severity != Scripting::ErrorSeverity::Warning)
				return; // front end has a hard error: the compiler stops before lowering

		// Mirror CodeGenDriver::Lower's body loop exactly: lower every complete (non-local) function to
		// HIR into ONE shared tracker, re-collecting records at a fixpoint because query-time template
		// instantiation can add new bodies while lowering. No LLVM is emitted - only the diagnostics are
		// kept, which is precisely what the compiler reports for type errors in function bodies.
		auto& model = m_Compiler->GetProgramModelForLowering();
		Scripting::ErrorTracker loweringErrors;
		std::set<const Scripting::FunctionRecord*> emitted;
		bool changed = true;
		while (changed)
		{
			changed = false;
			Core::Vector<const Scripting::FunctionRecord*> all;
			model.AllFunctionRecords(all);
			for (const auto* rec : all)
			{
				if (!rec->bodyNode || emitted.count(rec) || rec->isLocal)
					continue; // declaration only / already lowered / nested in a body (lowered with its parent)
				emitted.insert(rec);
				(void)Scripting::LowerFunction(model, loweringErrors, rec);
				changed = true;
			}
		}
		for (const auto& err : loweringErrors.GetErrors())
			m_LoweringErrors.Push(err);
	}

	void CompilationSession::ApplyOpenDocumentOverrides()
	{
		for (const auto& [uri, text] : m_OpenDocuments)
		{
			uint32 id = FindSourceIdByUri(uri);
			if (id != 0)
			{
				m_Compiler->GetSourceManager().SetSourceText(id, text);
			}
			else
			{
				// A new unsaved file that is not part of the scanned folder: compile it from memory.
				m_Compiler->GetSourceManager().RegisterInMemory(DisplayNameForUri(uri), text);
			}
		}
	}

	// Mirror the compiler's preprocess phase (see src/Source.cpp): macro expansion, then CBind
	// interop bindings for extern "c"/"c++" includes. Bindings are declarations-only - the LSP
	// never compiles or links foreign code. Without this, symbols generated from C/C++ headers
	// would be reported as undefined.
	static uint32 CountLines(const std::string& text)
	{
		uint32 n = 0;
		for (char c : text)
			if (c == '\n') ++n;
		return n; // lines of content above the last newline; a trailing line without \n adds nothing
	}

		static std::string PercentDecode(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		for (size_t i = 0; i < s.size(); ++i)
			if (s[i] == '%' && i + 2 < s.size() && isxdigit(static_cast<unsigned char>(s[i + 1])) && isxdigit(static_cast<unsigned char>(s[i + 2])))
			{
				auto hex = [](char c) { return static_cast<int>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10); };
				out.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
				i += 2;
			}
			else out.push_back(s[i]);
		return out;
	}

#ifdef _WIN32
	static std::string NormalizeForCompare(const std::string& path)
	{
		std::string p = PercentDecode(path);
		std::replace(p.begin(), p.end(), '\\', '/');
		for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return p;
	}

	static bool HasPrefixInsensitive(const std::string& path, const std::string& prefix, std::string& outRelative)
	{
		std::string np = NormalizeForCompare(path);
		std::string npre = NormalizeForCompare(prefix);
		if (np.compare(0, npre.size(), npre) != 0)
			return false;
		outRelative = path.substr(prefix.size()); // keep the document's own casing for display names
		return true;
	}
#else
	static bool HasPrefixInsensitive(const std::string& path, const std::string& prefix, std::string& outRelative)
	{
		if (path.compare(0, prefix.size(), prefix) != 0)
			return false;
		outRelative = path.substr(prefix.size());
		return true;
	}
#endif


void CompilationSession::RunPreprocessPhase()
	{
		// As C4.exe does: each on-disk file (an open document's editor text is already its source) starts with its
		// project's macros, and an include carries what the included file defines.
		std::vector<Scripting::MacroDefine> defines;
		for (const auto& [name, value] : m_Defines)
			defines.push_back({ name, value });
		const auto& sources = m_Compiler->GetSourceManager();
		auto macroResults = Scripting::ExpandSourceMacros(sources, Scripting::PredefinedMacros(false, m_Target, defines));
		m_Compiler->AddMacroDiagnostics(macroResults);

		std::unordered_map<std::string, uint32> projectOf;
		for (uint32 id = 1; id <= sources.Count(); ++id)
			if (sources.HasInfo(id)) projectOf[sources.GetInfo(id).absPath] = sources.GetInfo(id).project;
		std::vector<Scripting::InteropFileInput> texts;
		texts.reserve(macroResults.size());
		for (const auto& r : macroResults)
		{
			auto owner = projectOf.find(r.absPath);
			std::vector<std::string> folders = owner != projectOf.end() && !m_ProjectsFile.empty()
				? sources.FoldersReachedFrom(owner->second) : std::vector<std::string>();
			texts.push_back({ r.name, r.absPath, r.text, std::move(folders) });
		}

		std::string objDir = (fs::temp_directory_path() / "c4_lsp_interop").string();
		fs::create_directories(objDir);

		auto interop = Scripting::InteropPass::RunFolder(BuildPreprocessRoots(), texts, m_IncludePaths, objDir, m_Target.Flags(), false, true);

		std::map<std::string, std::string> macroByAbs;
		for (const auto& mr : macroResults)
			macroByAbs[mr.absPath] = mr.text;

		m_BindingStartLine.clear();
		m_BindingDirectiveLine.clear();
		// Replace each source's text with its effective form (macro-expanded + bindings) so the
		// parser sees exactly what the compiler would parse. The bindings follow the user's text, so
		// the user's lines are the editor's; record where the bindings start and the editor line of the
		// `extern ... include` directive that produced them (the only place a generated symbol's
		// definition is visible to the user).
		for (uint32 id = 1; id <= m_Compiler->SourceCount(); ++id)
		{
			const auto& info = m_Compiler->GetSourceManager().GetInfo(id);
			std::string effective;
			bool hasBindings = false;
			size_t directiveOffset = SIZE_MAX;
			for (const auto& ir : interop.files)
				if (ir.absPath == info.absPath) { effective = ir.text; hasBindings = true; directiveOffset = ir.directiveOffset; break; }
			if (!hasBindings)
			for (const auto& mr : macroResults)
				if (mr.absPath == info.absPath) { effective = mr.text; break; }
			if (effective.empty())
				continue;

			m_Compiler->GetSourceManager().SetSourceText(id, effective);

			if (hasBindings)
			{
				auto it = macroByAbs.find(info.absPath);
				m_BindingStartLine[id] = (it != macroByAbs.end() ? CountLines(it->second) : 0) + 1;

				// Editor line (1-based) of the directive, computed against the original text.
				if (directiveOffset != SIZE_MAX && it != macroByAbs.end())
					m_BindingDirectiveLine[id] = static_cast<uint32>(std::count(it->second.begin(), it->second.begin() + directiveOffset, '\n')) + 1;
			}
		}
	}

	Protocol::Range CompilationSession::ToEditorRange(uint32 sourceId, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end) const
	{
		auto conv = [](const Scripting::SourceLocation& loc) -> Protocol::Position {
			Protocol::Position p;
			p.line = loc.line > 0 ? loc.line - 1 : 0;
			p.character = loc.column > 0 ? loc.column - 1 : 0;
			return p;
		};

		// A declaration in the appended CBind binding block has no visible editor line. Redirect it to the
		// `extern ... include` directive that produced those bindings, so F12/highlight land on a real,
		// meaningful line instead of past the end of the file.
		auto bindings = m_BindingStartLine.find(sourceId);
		if (bindings != m_BindingStartLine.end() && (start.line >= bindings->second || end.line >= bindings->second))
		{
			auto it = m_BindingDirectiveLine.find(sourceId);
			uint32 line = (it != m_BindingDirectiveLine.end()) ? it->second : 1; // 1-based
			Protocol::Position p;
			p.line = (line > 0) ? line - 1 : 0;   // -> 0-based editor line
			p.character = 0;
			return Protocol::Range{ p, p };
		}

		Protocol::Range r;
		r.start = conv(start);
		r.end = conv(end);
		return r;
	}

	Protocol::Location CompilationSession::ToEditorLocation(const Scripting::SourceLocation& start, const Scripting::SourceLocation& end) const
	{
		uint32 declSourceId = start.sourceId;
		return Protocol::Location{ UriForSourceId(declSourceId), ToEditorRange(declSourceId, start, end) };
	}

	std::string CompilationSession::UriForSourceId(uint32 sourceId) const
	{
		// A file on disk is where it is (a project's, an -I folder's or Bedrock's); an in-memory source has only a name.
		const auto& info = m_Compiler->GetSourceManager().GetInfo(sourceId);
		if (!info.absPath.empty())
			return Protocol::DocumentUri::ToUri(info.absPath);
		if (m_RootFolder.empty())
			return Protocol::DocumentUri::ToUri(info.displayName);
		return Protocol::DocumentUri::ToUri(m_RootFolder + "/" + info.displayName);
	}

	bool IsAbsolutePath(const std::string& p)
	{
		if (p.size() >= 2 && p[1] == ':') return true; // drive letter (C:\ or C:/)
		return !p.empty() && (p.front() == '/' || p.front() == '\\');
	}

	std::string CompilationSession::GetEditorText(const std::string& uri) const
	{
		auto openIt = m_OpenDocuments.find(uri);
		if (openIt != m_OpenDocuments.end())
			return openIt->second;

		std::string displayName = DisplayNameForUri(uri);
		auto memIt = m_InMemorySources.find(displayName);
		if (memIt != m_InMemorySources.end())
			return memIt->second;

		fs::path path = IsAbsolutePath(displayName) ? fs::path(displayName) : fs::path(m_RootFolder) / displayName;
		std::ifstream f(path, std::ios::binary);
		if (!f.is_open())
			return std::string();
		std::ostringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	uint32 CompilationSession::FindSourceIdByUri(const std::string& uri) const
	{
		const auto& sources = m_Compiler->GetSourceManager();
		if (uint32 id = sources.FindByAbsPath(Protocol::DocumentUri::FromUri(uri))) return id;
		const uint32 named = sources.FindSourceId(DisplayNameForUri(uri));
		return named != 0 && sources.GetInfo(named).absPath.empty() ? named : 0; // a name alone identifies only an in-memory source
	}

	std::string CompilationSession::DisplayNameForUri(const std::string& uri) const
	{
		std::string path = Protocol::DocumentUri::FromUri(uri); // percent-decoded absolute path
		std::replace(path.begin(), path.end(), '\\', '/');

		if (!m_RootFolder.empty())
		{
			const std::string prefix = m_RootFolder + "/";
			std::string relative;
			if (HasPrefixInsensitive(path, prefix, relative)) // drive-letter/case-insensitive on Windows
				return PercentDecode(relative);
		}

		size_t lastSlash = path.find_last_of('/');
		return lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
	}

	std::vector<std::string> CompilationSession::BuildPreprocessRoots() const
	{
		std::vector<std::string> roots;
		if (!m_RootFolder.empty())
			roots.push_back(m_RootFolder);
		for (const auto& dir : m_IncludePaths) // -I dirs join the search space, like the compiler's macro pass does with its root folders
			if (!dir.empty() && fs::is_directory(dir))
				roots.push_back(dir);
		return roots;
	}
} // namespace C4LSP::Server
