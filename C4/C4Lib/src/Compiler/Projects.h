#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Scripting
{
	namespace fs = std::filesystem;

	/// One project of a build (D-59, docs/language/15-projects.md).
	struct Project
	{
		std::string name;
		std::vector<fs::path> sources;  // folders or files, absolute
		std::vector<fs::path> includes; // folders its includes also search, absolute
		std::map<std::string, std::string> defines;
		std::vector<std::string> depends;
	};

	/// A build's projects as written: the projects and the name of the root.
	struct ProjectList
	{
		std::vector<Project> projects;
		std::string root;
	};

	struct ProjectSetResult;

	/// The projects of one build, checked: unique names, known dependencies, no cycle, no file in two projects.
	class ProjectSet
	{
	public:
		static ProjectSetResult Check(ProjectList list);
		/// The plain command line: the inputs and every -I folder are one project, as before projects existed.
		static ProjectSet OneProject(std::vector<fs::path> sources);

		uint32_t Count() const { return static_cast<uint32_t>(m_Projects.size()); }
		const Project& At(uint32_t project) const { return m_Projects[project]; }
		uint32_t Root() const { return m_Root; }

		/// The project whose sources hold `file` (the deepest source folder wins); nothing when no project does.
		std::optional<uint32_t> OwnerOf(const fs::path& file) const;
		/// `user` itself, then every project it depends on, directly first.
		const std::vector<uint32_t>& Uses(uint32_t user) const { return m_Uses[user]; }
		bool CanUse(uint32_t user, uint32_t used) const;
		/// A project's source folders then include folders: where its includes look.
		std::vector<fs::path> SearchFolders(uint32_t project) const;

	private:
		std::vector<Project> m_Projects;
		uint32_t m_Root = 0;
		std::vector<std::vector<uint32_t>> m_Uses;
	};

	/// A checked set, or why the list is not one.
	struct ProjectSetResult
	{
		std::optional<ProjectSet> set;
		std::string error;
	};
}
