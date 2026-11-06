#include "Server/Program.h"
#include "Server/CompilationSession.h"
#include "Server/ScriptScopes.h"
#include "Compiler/ProjectFile.h"
#include "Compiler/Toolchain.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

namespace C4LSP::Server
{
	namespace fs = std::filesystem;

	namespace
	{
		fs::path PathOf(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

		std::string StampOf(const std::string& file)
		{
			std::error_code ec;
			const auto size = fs::file_size(PathOf(file), ec);
			if (ec) return "";
			const auto time = fs::last_write_time(PathOf(file), ec).time_since_epoch().count();
			return ec ? "" : std::to_string(size) + "|" + std::to_string(time);
		}
	}

	Program::Program(ProgramOptions options) : m_Options(std::move(options)) { ReadDescription(); }

	void Program::ReadDescription()
	{
		m_Projects.reset();
		m_Owned.clear();
		m_Stamp = m_Options.projectsFile.empty() ? "" : StampOf(m_Options.projectsFile);
		if (m_Stamp.empty()) return;
		const Scripting::ProjectFileResult read = Scripting::ReadProjectFile(PathOf(m_Options.projectsFile));
		if (!read.error.empty()) return;
		Scripting::ProjectSetResult checked = Scripting::ProjectSet::Check(read.list);
		if (checked.set) m_Projects = std::move(*checked.set);
	}

	bool Program::Refresh()
	{
		if (m_Options.projectsFile.empty() || StampOf(m_Options.projectsFile) == m_Stamp) return false;
		ReadDescription();
		return true;
	}

	bool Program::Owns(const std::string& path) const
	{
		if (m_Options.library) return InLibrary(path);
		if (!m_Projects) return false;
		auto known = m_Owned.find(path);
		if (known != m_Owned.end()) return known->second;
		const bool owned = m_Projects->OwnerOf(PathOf(path)).has_value();
		m_Owned.emplace(path, owned);
		return owned;
	}

	std::set<std::string> Program::ProjectNames() const
	{
		if (m_Options.library) return { kLibraryProject };
		std::set<std::string> names;
		if (m_Projects)
			for (uint32 p = 0; p < m_Projects->Count(); ++p) names.insert(m_Projects->At(p).name);
		return names;
	}

	bool Program::InLibrary(const std::string& path) const
	{
		const std::string library = ComparablePath(m_Options.libraryFolder.empty() ? Scripting::Toolchain::Bedrock().generic_string() : m_Options.libraryFolder);
		const std::string file = ComparablePath(path);
		return file.size() > library.size() && file.compare(0, library.size(), library) == 0 && file[library.size()] == '/';
	}

	std::set<std::string> Program::ProjectsReaching(const std::string& path) const
	{
		if (m_Options.library) return InLibrary(path) ? std::set<std::string>{ kLibraryProject } : std::set<std::string>();
		if (!m_Projects) return {};
		const std::optional<uint32_t> owner = m_Projects->OwnerOf(PathOf(path));
		if (!owner) return InLibrary(path) ? ProjectNames() : std::set<std::string>();
		std::set<std::string> reaching;
		for (uint32 p = 0; p < m_Projects->Count(); ++p)
			if (m_Projects->CanUse(p, *owner)) reaching.insert(m_Projects->At(p).name);
		return reaching;
	}

	namespace
	{
		std::optional<std::string> FileText(const std::string& path)
		{
			std::ifstream file(PathOf(path), std::ios::binary);
			if (!file.is_open()) return std::nullopt;
			std::ostringstream text;
			text << file.rdbuf();
			return text.str();
		}

		// Whether what `session` compiled for `uri` is its file as it is on disk (or nothing: it compiled no such file). The
		// compiler reads a file without its UTF-8 byte-order mark.
		bool CompiledAsOnDisk(const CompilationSession& session, const std::string& uri)
		{
			const std::optional<size_t> compiled = session.CompiledTextHash(uri);
			if (!compiled) return true;
			const std::optional<std::string> disk = FileText(Protocol::DocumentUri::FromUri(uri));
			if (!disk) return false;
			constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
			const std::string_view text = std::string_view(*disk).substr(std::string_view(*disk).substr(0, kUtf8Bom.size()) == kUtf8Bom ? kUtf8Bom.size() : 0);
			return std::hash<std::string_view>{}(text) == *compiled;
		}

		// The fractional part of the golden ratio in 64 bits: a hash combine's spread (boost's hash_combine).
		constexpr size_t kGoldenRatio = 0x9e3779b97f4a7c15ull;

		size_t Mixed(size_t seed, size_t value) { return seed ^ (value + kGoldenRatio + (seed << 6) + (seed >> 2)); }

		size_t ContentHash(const Core::Vector<Features::WorkspaceSymbol>& symbols)
		{
			const std::hash<std::string> text;
			size_t hash = symbols.Count();
			for (const Features::WorkspaceSymbol& symbol : symbols)
			{
				hash = Mixed(hash, text(symbol.name));
				hash = Mixed(hash, text(symbol.containerName));
				hash = Mixed(hash, text(symbol.location.uri));
				hash = Mixed(hash, static_cast<size_t>(symbol.kind));
				const Protocol::Range& r = symbol.location.range;
				hash = Mixed(hash, (static_cast<size_t>(r.start.line) << 32) ^ r.start.character);
				hash = Mixed(hash, (static_cast<size_t>(r.end.line) << 32) ^ r.end.character);
			}
			return hash;
		}

		bool SameSymbols(const Core::Vector<Features::WorkspaceSymbol>& a, const Core::Vector<Features::WorkspaceSymbol>& b)
		{
			if (a.Count() != b.Count()) return false;
			for (uint32 i = 0; i < a.Count(); ++i)
			{
				const Protocol::Range& x = a[i].location.range;
				const Protocol::Range& y = b[i].location.range;
				if (a[i].name != b[i].name || a[i].containerName != b[i].containerName || a[i].kind != b[i].kind || a[i].location.uri != b[i].location.uri ||
					x.start.line != y.start.line || x.start.character != y.start.character || x.end.line != y.end.line || x.end.character != y.end.character)
					return false;
			}
			return true;
		}
	}

	std::vector<std::shared_ptr<const FileSymbols>> SharedFileSymbols::Share(Core::Vector<Features::WorkspaceSymbol> symbols)
	{
		for (auto it = m_ByContent.begin(); it != m_ByContent.end();) // the copies no index holds any more
		{
			std::erase_if(it->second, [](const std::weak_ptr<const FileSymbols>& copy) { return copy.expired(); });
			it = it->second.empty() ? m_ByContent.erase(it) : std::next(it);
		}
		std::map<std::string, Core::Vector<Features::WorkspaceSymbol>> byFile;
		for (Features::WorkspaceSymbol& symbol : symbols)
			byFile[symbol.location.uri].Push(std::move(symbol));
		std::vector<std::shared_ptr<const FileSymbols>> shared;
		shared.reserve(byFile.size());
		for (auto& [uri, own] : byFile)
		{
			std::vector<std::weak_ptr<const FileSymbols>>& copies = m_ByContent[ContentHash(own)];
			std::shared_ptr<const FileSymbols> same;
			for (const std::weak_ptr<const FileSymbols>& known : copies)
			{
				std::shared_ptr<const FileSymbols> copy = known.lock();
				if (copy && SameSymbols(copy->symbols, own))
				{
					same = std::move(copy);
					break;
				}
			}
			if (!same)
			{
				auto made = std::make_shared<FileSymbols>();
				made->symbols = std::move(own);
				same = made;
				copies.push_back(same);
			}
			shared.push_back(std::move(same));
		}
		return shared;
	}

	bool Program::AnalysisIsOf(const std::map<std::string, size_t>& taken) const
	{
		if (!session || stale) return false;
		for (const auto& [uri, hash] : taken)
			if (session->CompiledTextHash(uri) != hash) return false;
		for (const auto& [uri, hash] : analyzedWith)
			if (!taken.count(uri) && !CompiledAsOnDisk(*session, uri)) return false;
		return true;
	}

	std::shared_ptr<CompilationSession> Program::Analyze(const std::string& rootFolder, const EditorDocuments& documents) const
	{
		auto session = std::make_shared<CompilationSession>();
		if (!rootFolder.empty()) session->SetRootFolder(rootFolder);
		session->SetIncludePaths(m_Options.includePaths);
		session->SetDefines(m_Options.defines);
		session->SetTarget(m_Options.Target());
		if (!m_Options.libraryFolder.empty()) session->SetLibraryFolder(m_Options.libraryFolder);
		session->SetProjectsFile(m_Options.projectsFile);
		for (const auto& [uri, text] : documents) session->SetOpenDocument(uri, text);
		session->Recompile();
		return session;
	}
} // namespace C4LSP::Server
