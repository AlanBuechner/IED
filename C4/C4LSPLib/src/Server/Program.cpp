#include "Server/Program.h"
#include "Server/CompilationSession.h"
#include "Server/ScriptScopes.h"
#include "Compiler/ProjectFile.h"
#include "Compiler/Toolchain.h"
#include <filesystem>

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
		if (!m_Projects) return false;
		auto known = m_Owned.find(path);
		if (known != m_Owned.end()) return known->second;
		const bool owned = m_Projects->OwnerOf(PathOf(path)).has_value();
		m_Owned.emplace(path, owned);
		return owned;
	}

	std::set<std::string> Program::ProjectNames() const
	{
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
		if (!m_Projects) return {};
		const std::optional<uint32_t> owner = m_Projects->OwnerOf(PathOf(path));
		if (!owner) return InLibrary(path) ? ProjectNames() : std::set<std::string>();
		std::set<std::string> reaching;
		for (uint32 p = 0; p < m_Projects->Count(); ++p)
			if (m_Projects->CanUse(p, *owner)) reaching.insert(m_Projects->At(p).name);
		return reaching;
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
