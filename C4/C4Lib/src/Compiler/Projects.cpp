#include "Compiler/Projects.h"
#include <algorithm>
#include <cctype>
#include <functional>

namespace Scripting
{
	namespace
	{
		// A path in the form two spellings of one place compare equal in (Windows paths ignore case).
		std::string Comparable(const fs::path& path)
		{
			std::string text = path.lexically_normal().generic_string();
			while (text.size() > 1 && text.back() == '/') text.pop_back();
#ifdef _WIN32
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
			return text;
		}

		bool Contains(const fs::path& folder, const fs::path& path)
		{
			const std::string outer = Comparable(folder);
			const std::string inner = Comparable(path);
			return inner == outer || (inner.size() > outer.size() && inner.compare(0, outer.size(), outer) == 0 && inner[outer.size()] == '/');
		}

		std::optional<uint32_t> IndexOf(const std::vector<Project>& projects, const std::string& name)
		{
			for (uint32_t i = 0; i < projects.size(); ++i)
				if (projects[i].name == name) return i;
			return std::nullopt;
		}

		std::string NamesAndDependencies(const std::vector<Project>& projects)
		{
			for (uint32_t i = 0; i < projects.size(); ++i)
			{
				if (projects[i].name.empty()) return "project " + std::to_string(i + 1) + " has no name";
				if (IndexOf(projects, projects[i].name) != i) return "two projects are named '" + projects[i].name + "'";
				for (const std::string& used : projects[i].depends)
				{
					if (used == projects[i].name) return "project '" + used + "' depends on itself";
					if (!IndexOf(projects, used)) return "project '" + projects[i].name + "' depends on '" + used + "', which the build does not describe";
				}
			}
			return "";
		}

		// "A -> B -> A" for the first dependency cycle, or "" when there is none.
		std::string Cycle(const std::vector<Project>& projects)
		{
			enum class Mark { New, Open, Done };
			std::vector<Mark> marks(projects.size(), Mark::New);
			std::vector<uint32_t> path;
			std::function<std::string(uint32_t)> visit = [&](uint32_t at) -> std::string
			{
				if (marks[at] == Mark::Done) return "";
				if (marks[at] == Mark::Open)
				{
					std::string chain;
					for (auto it = std::find(path.begin(), path.end(), at); it != path.end(); ++it) chain += projects[*it].name + " -> ";
					return chain + projects[at].name;
				}
				marks[at] = Mark::Open;
				path.push_back(at);
				for (const std::string& used : projects[at].depends)
					if (std::string found = visit(*IndexOf(projects, used)); !found.empty()) return found;
				path.pop_back();
				marks[at] = Mark::Done;
				return "";
			};
			for (uint32_t i = 0; i < projects.size(); ++i)
				if (std::string found = visit(i); !found.empty()) return found;
			return "";
		}

		std::string Overlap(const std::vector<Project>& projects)
		{
			for (uint32_t a = 0; a < projects.size(); ++a)
				for (uint32_t b = a + 1; b < projects.size(); ++b)
					for (const fs::path& first : projects[a].sources)
						for (const fs::path& second : projects[b].sources)
							if (Contains(first, second) || Contains(second, first))
								return "'" + (Contains(first, second) ? second : first).generic_string() + "' is in the sources of both '" +
									projects[a].name + "' and '" + projects[b].name + "'";
			return "";
		}

		// `from` itself, then what it depends on, nearest first.
		std::vector<uint32_t> Closure(const std::vector<Project>& projects, uint32_t from)
		{
			std::vector<uint32_t> order = { from };
			for (size_t at = 0; at < order.size(); ++at)
				for (const std::string& used : projects[order[at]].depends)
					if (const uint32_t index = *IndexOf(projects, used); std::find(order.begin(), order.end(), index) == order.end())
						order.push_back(index);
			return order;
		}
	}

	ProjectSetResult ProjectSet::Check(ProjectList list)
	{
		ProjectSetResult result;
		const std::optional<uint32_t> root = IndexOf(list.projects, list.root);
		if (list.root.empty()) result.error = "the build names no root project";
		else if (!root) result.error = "the root project '" + list.root + "' is not described";
		else if (std::string why = NamesAndDependencies(list.projects); !why.empty()) result.error = why;
		else if (std::string cycle = Cycle(list.projects); !cycle.empty()) result.error = "the projects depend on each other in a cycle: " + cycle;
		else if (std::string overlap = Overlap(list.projects); !overlap.empty()) result.error = overlap;
		if (!result.error.empty()) return result;

		ProjectSet set;
		set.m_Projects = std::move(list.projects);
		set.m_Root = *root;
		for (uint32_t i = 0; i < set.m_Projects.size(); ++i) set.m_Uses.push_back(Closure(set.m_Projects, i));
		for (uint32_t i = 0; i < set.m_Projects.size(); ++i)
			if (!set.CanUse(set.m_Root, i))
			{
				result.error = "project '" + set.m_Projects[i].name + "' is described, but the root '" + set.m_Projects[set.m_Root].name + "' does not use it";
				return result;
			}
		set.FindSearchFolders();
		result.set = std::move(set);
		return result;
	}

	ProjectSet ProjectSet::OneProject(std::vector<fs::path> sources)
	{
		ProjectSet set;
		Project program;
		program.name = "main";
		program.sources = std::move(sources);
		set.m_Projects.push_back(std::move(program));
		set.m_Uses.push_back({ 0 });
		set.FindSearchFolders();
		return set;
	}

	std::optional<uint32_t> ProjectSet::OwnerOf(const fs::path& file) const
	{
		for (uint32_t i = 0; i < m_Projects.size(); ++i)
			for (const fs::path& source : m_Projects[i].sources)
				if (Contains(source, file)) return i;
		return std::nullopt;
	}

	bool ProjectSet::CanUse(uint32_t user, uint32_t used) const
	{
		const std::vector<uint32_t>& uses = m_Uses[user];
		return std::find(uses.begin(), uses.end(), used) != uses.end();
	}

	void ProjectSet::FindSearchFolders()
	{
		m_SearchFolders.clear();
		for (const Project& project : m_Projects)
		{
			std::vector<fs::path>& folders = m_SearchFolders.emplace_back();
			for (const fs::path& source : project.sources) folders.push_back(fs::is_directory(source) ? source : source.parent_path());
			for (const fs::path& include : project.includes) folders.push_back(include);
		}
	}
}
