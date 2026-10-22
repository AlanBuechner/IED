#pragma once
#include "Source.h"
#include "Compiler/Projects.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Scripting
{

	// How much of a folder RegisterFolder takes: everything under it, or only the files directly in it.
	enum class FolderScan { Recursive, TopOnly };

	struct SourceInfo
	{
		uint32 id = 0;
		std::string displayName; // relative path within the owning root folder (may repeat across roots)
		std::string baseDir; // absolute directory the file was registered relative to (for disk include resolution)
		std::string absPath; // normalized absolute path - the source's unique identity (empty for in-memory sources)
		Ref<Source> source;
		uint32 project = 0;   // the project the file belongs to (docs/language/15-projects.md)
		bool library = false; // a file of the default library (Bedrock)
		bool byName = false;  // registered by itself (in memory, or one file): includes find it by its display name
	};

	/// The file an include names, or why it names none the includer may use.
	struct IncludeTarget
	{
		uint32 id = 0;
		std::string error; // a file of a project the includer does not depend on, or a path two dependencies have
	};

	class SourceManager
	{
	public:
		SourceManager() : m_Projects(ProjectSet::OneProject({})) {}

		/// The build's projects (docs/language/15-projects.md); set before registering their sources. Without it the
		/// whole program is one project.
		void SetProjects(ProjectSet projects) { m_Projects = std::move(projects); }
		const ProjectSet& Projects() const { return m_Projects; }

		// Registration - folder registration is include path + file discovery
		uint32 RegisterFile(const fs::path& path);
		uint32 RegisterInMemory(const fs::path& name, const std::string& sourceText);
		uint32 RegisterFolder(const fs::path& folder, FolderScan scan = FolderScan::Recursive);
		/// A source whose text is given, at `absPath` in no folder on disk: a file of a script's host interface, or a
		/// script given as text (docs/runtime/04 section 4). It is `project`'s; a library file's includes resolve in
		/// the library. 0 when `absPath` is registered already.
		uint32 RegisterVirtual(const std::string& absPath, const std::string& displayName, const std::string& text, uint32 project, bool library);
		/// A folder `project`'s includes search, though nothing on disk is registered from it (an interface's folder).
		void AddFolder(const std::string& folder, uint32 project);
		/// Where library files' includes resolve, and the prelude every unit includes (an interface's).
		void SetLibrary(const std::string& root, uint32 preludeId);

		/// The file `include` names from `requester`: beside it, then in its project's folders, then in the folders of
		/// the projects its project depends on, then in the default library (docs/language/15-projects.md).
		IncludeTarget ResolveInclude(const std::string& include, uint32 requester) const;
		/// The folders the includes of `project`'s files search: its own, then its dependencies', nearest first.
		std::vector<std::string> FoldersReachedFrom(uint32 project) const;
		uint32 ResolveIncludePath(const Core::String& includePath, uint32 requestingSourceId) const
		{
			return ResolveInclude(includePath.c_str(), requestingSourceId).id;
		}

		/// The default library at `root` (Bedrock) is opt-in by include: the library files the registered
		/// program sources include are registered the same way as an umbrella entry. A program that
		/// includes none of it compiles none of it, and the program's own file wins a name both have.
		/// Its prelude (Prelude/prelude.ch) is registered always: every unit includes it (D-43).
		uint32 RegisterIncludedLibraryFiles(const fs::path& root);

		/// The prelude's source id; 0 when no default library with a prelude was registered.
		uint32 PreludeId() const { return m_PreludeId; }

		/// The registered folders, normalized, in registration order, each with its project.
		const std::vector<std::pair<std::string, uint32>>& Folders() const { return m_Folders; }
		/// The default library's root, normalized; empty when none was registered.
		const std::string& LibraryRoot() const { return m_LibraryRoot; }

	private:
		uint32 RegisterLibraryClosure(const std::string& root, std::vector<fs::path> entries);
		uint32 ProjectOf(const std::string& absPath) const;
		std::vector<std::string> FoldersOf(uint32 project) const;
		uint32 InFolder(const std::string& folder, const std::string& written) const;
		uint32 BesideRequester(const SourceInfo& requester, const std::string& written) const;
		uint32 InProject(uint32 project, const std::string& written) const;
		IncludeTarget InDependencies(uint32 project, const std::string& written) const;
		std::string OutOfReach(uint32 project, const std::string& written) const;

	public:
		uint32 Count() const { return static_cast<uint32>(m_Sources.size()); }

		bool HasInfo(uint32 id) const { return m_Sources.find(id) != m_Sources.end(); }

		/// Find a registered source by its display name (relative path for folder-registered
		/// files, file name for in-memory files). Returns 0 when not found. Used by the LSP to
		/// map an editor document back to its compiled source so edits can be applied and the
		/// right compilation unit located.
		uint32 FindSourceId(const std::string& displayName) const
		{
			auto it = m_NameToId.find(displayName);
			return it != m_NameToId.end() ? it->second : 0;
		}

		/// The source registered from `path` (any slashes; Windows paths match whatever their case); 0 when none.
		uint32 FindByAbsPath(const std::string& path) const;

		// this assumes that the source info exists
		SourceInfo& GetInfo(uint32 id) { return m_Sources.at(id); }
		const SourceInfo& GetInfo(uint32 id) const { return m_Sources.at(id); }

		/// Replace a registered source's text (e.g. with macro-expanded text). Rebuilds
		/// the line table from the new content. No-op if the id is unknown.
		void SetSourceText(uint32 id, const std::string& text)
		{
			auto it = m_Sources.find(id);
			if (it == m_Sources.end()) return;
			it->second.source = Source::FromMemory(text);
		}

		Core::String GetLineText(uint32 id, uint32 line) const
		{
			auto it = m_Sources.find(id);
			if (it == m_Sources.end() || line == 0 || !it->second.source) return Core::String();
			return it->second.source->GetLineText(line);
		}

	private:
		// One entry of a folder being registered: 1 when it is a C4 source now registered, else 0.
		uint32 RegisterFolderFile(const fs::path& path, Ref<Source> source, const fs::path& folder, const std::string& baseDir);

		std::string NormalizePath(const std::string& path) const
		{
			std::string result = path;
			std::replace(result.begin(), result.end(), '\\', '/');
			return result;
		}

		ProjectSet m_Projects;
		std::vector<std::pair<std::string, uint32>> m_Folders; // registered folders and the project each belongs to
		std::string m_LibraryRoot;
		std::unordered_map<uint32, SourceInfo> m_Sources;
		std::unordered_set<std::string> m_AbsPaths;
		std::unordered_map<std::string, uint32> m_NameToId;
		std::unordered_map<std::string, uint32> m_AbsPathToId;
		uint32 m_NextId = 1;
		uint32 m_PreludeId = 0;
	};
}
