#include "SourceManager.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace Scripting
{

	uint32 SourceManager::RegisterFile(const fs::path& path)
	{
		std::ifstream test(path, std::ios::binary);
		if (!test.is_open()) return 0;

		std::string absPath = NormalizePath(fs::absolute(path).lexically_normal().string());
		if (m_AbsPaths.count(absPath)) return 0;

		uint32 id = m_NextId++;
		m_AbsPaths.insert(absPath);

		std::string name = path.filename().string();
		SourceInfo info{ id, name, NormalizePath(fs::absolute(path).parent_path().string()), absPath, CreateRef<Source>(path) };
		info.project = ProjectOf(absPath);
		info.byName = true;
		m_Sources.emplace(id, std::move(info));
		m_NameToId[name] = id;
		m_AbsPathToId[absPath] = id;

		return id;
	}

	uint32 SourceManager::RegisterInMemory(const fs::path& name, const std::string& sourceText)
	{
		std::string nameStr = name.filename().string();
		if (m_NameToId.count(nameStr)) return 0;

		uint32 id = m_NextId++;
		SourceInfo info{ id, nameStr, std::string(), std::string(), Source::FromMemory(sourceText) };
		info.project = m_Projects.Root();
		info.byName = true;
		m_Sources.emplace(id, std::move(info));
		m_NameToId[nameStr] = id;

		return id;
	}

	uint32 SourceManager::RegisterVirtual(const std::string& absPath, const std::string& displayName, const std::string& text, uint32 project,
		bool library)
	{
		const std::string path = NormalizePath(absPath);
		if (m_AbsPaths.count(path)) return 0;
		const uint32 id = m_NextId++;
		SourceInfo info{ id, displayName, NormalizePath(fs::path(path).parent_path().string()), path, Source::FromMemory(text) };
		info.project = project;
		info.library = library;
		m_Sources.emplace(id, std::move(info));
		m_AbsPaths.insert(path);
		m_NameToId[displayName] = id;
		m_AbsPathToId[path] = id;
		return id;
	}

	void SourceManager::AddFolder(const std::string& folder, uint32 project)
	{
		const std::string normalized = NormalizePath(folder);
		if (std::find(m_Folders.begin(), m_Folders.end(), std::make_pair(normalized, project)) == m_Folders.end())
			m_Folders.push_back({ normalized, project });
	}

	void SourceManager::SetLibrary(const std::string& root, uint32 preludeId)
	{
		m_LibraryRoot = NormalizePath(root);
		m_PreludeId = preludeId;
	}

	uint32 SourceManager::RegisterFolder(const fs::path& folder)
	{
		if (!fs::is_directory(folder)) return 0;

		std::string baseDir = NormalizePath(fs::absolute(folder).lexically_normal().string());
		while (baseDir.size() > 1 && baseDir.back() == '/') baseDir.pop_back();
		const uint32 project = ProjectOf(baseDir);
		if (std::find(m_Folders.begin(), m_Folders.end(), std::make_pair(baseDir, project)) == m_Folders.end())
			m_Folders.push_back({ baseDir, project });

		uint32 count = 0;

		for (const auto& entry : fs::recursive_directory_iterator(folder))
		{
			if (!entry.is_regular_file()) continue;

			const auto ext = entry.path().extension().string();
			if (ext != ".cf" && ext != ".ch") continue;

			std::ifstream test(entry.path(), std::ios::binary);
			if (!test.is_open()) continue;

			std::string absPath = NormalizePath(fs::absolute(entry.path()).string());
			if (m_AbsPaths.count(absPath)) continue;

			uint32 id = m_NextId++;
			m_AbsPaths.insert(absPath);

			fs::path rel = fs::relative(entry.path(), folder);
			std::string relStr = NormalizePath(rel.string());

			SourceInfo info{ id, relStr, baseDir, absPath, CreateRef<Source>(entry.path()) };
			info.project = ProjectOf(absPath);
			m_Sources.emplace(id, std::move(info));
			m_NameToId[relStr] = id;
			m_AbsPathToId[absPath] = id;

			++count;
		}

		return count;
	}

	namespace
	{
		// The `include "..."` targets written in a source text (commented-out lines skipped).
		std::vector<std::string> ExtractIncludes(const std::string& text)
		{
			std::vector<std::string> out;
			size_t pos = 0;
			while (true) {
				size_t i = text.find("include", pos);
				if (i == std::string::npos) break;
				bool wordStart = (i == 0) || (!std::isalnum(static_cast<unsigned char>(text[i - 1])) && text[i - 1] != '_');
				bool commentedOut = false;
				for (size_t b = i; b > 0; --b) {
					if (text[b - 1] == '\n') break;
					if (text[b - 1] == '/' && b >= 2 && text[b - 2] == '/') { commentedOut = true; break; }
				}
				if (!wordStart || commentedOut) { pos = i + 1; continue; }
				size_t j = i + 7;
				while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
				if (j >= text.size() || text[j] != '"') { pos = i + 1; continue; }
				size_t close = text.find('"', j + 1);
				if (close == std::string::npos) break;
				out.push_back(text.substr(j + 1, close - j - 1));
				pos = close + 1;
			}
			return out;
		}
	}

	uint32 SourceManager::RegisterIncludedLibraryFiles(const fs::path& rootPath)
	{
		if (!fs::is_directory(rootPath)) return 0;
		std::string root = NormalizePath(fs::absolute(rootPath).string());
		std::vector<fs::path> entries;
		for (const auto& [id, info] : m_Sources)
			for (const std::string& inc : ExtractIncludes(info.source->Data()))
			{
				if (ResolveIncludePath(inc.c_str(), id) != 0) continue; // the program's own file
				fs::path candidate = fs::path(root) / inc;
				if (fs::is_regular_file(candidate)) entries.push_back(candidate);
			}
		const fs::path prelude = fs::path(root) / "Prelude" / "prelude.ch";
		const bool hasPrelude = fs::is_regular_file(prelude);
		if (entries.empty() && !hasPrelude) return 0;
		m_LibraryRoot = root;
		uint32 count = RegisterLibraryClosure(root, std::move(entries));
		if (!hasPrelude) return count;
		count += RegisterLibraryClosure(root, { prelude });
		m_PreludeId = m_AbsPathToId.at(NormalizePath(fs::absolute(prelude).string()));
		return count;
	}

	uint32 SourceManager::RegisterLibraryClosure(const std::string& root, std::vector<fs::path> queue)
	{
		std::set<std::string> queued;
		uint32 count = 0;
		while (!queue.empty()) {
			fs::path cur = queue.back(); queue.pop_back();
			std::string absCur = NormalizePath(fs::absolute(cur).lexically_normal().string());
			if (queued.count(absCur) || m_AbsPaths.count(absCur)) continue;
			queued.insert(absCur);

			uint32 id = m_NextId++;
			m_AbsPaths.insert(absCur);
			fs::path rel = fs::relative(cur, root);
			std::string relStr = NormalizePath(rel.string());
			SourceInfo info{ id, relStr, NormalizePath(fs::absolute(cur).parent_path().string()), absCur, CreateRef<Source>(cur) };
			info.project = m_Projects.Root();
			info.library = true;
			m_Sources.emplace(id, std::move(info));
			m_NameToId[relStr] = id;
			m_AbsPathToId[absCur] = id;
			++count;

			// Register the paired implementation file (.ch <-> .cf) in the same directory.
			std::string ext = cur.extension().string();
			if (ext == ".ch" || ext == ".cf") {
				fs::path paired = cur;
				paired.replace_extension(ext == ".ch" ? ".cf" : ".ch");
				if (fs::is_regular_file(paired)) queue.push_back(paired);
			}

			for (const auto& inc : ExtractIncludes(m_Sources.at(id).source->Data())) {
				fs::path c1 = fs::absolute(fs::path(cur.parent_path() / inc));
				if (fs::is_regular_file(c1)) { queue.push_back(c1); continue; }
				fs::path c2 = fs::absolute(fs::path(root) / inc);
				if (fs::is_regular_file(c2)) queue.push_back(c2);
			}
		}
		return count;
	}

	uint32 SourceManager::FindByAbsPath(const std::string& path) const
	{
		const std::string wanted = NormalizePath(fs::path(path).lexically_normal().string());
		auto it = m_AbsPathToId.find(wanted);
		if (it != m_AbsPathToId.end()) return it->second;
#ifdef _WIN32
		auto lower = [](std::string text) { for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return text; };
		const std::string folded = lower(wanted);
		for (const auto& [abs, id] : m_AbsPathToId)
			if (lower(abs) == folded) return id;
#endif
		return 0;
	}

	uint32 SourceManager::ProjectOf(const std::string& absPath) const
	{
		return m_Projects.OwnerOf(fs::path(absPath)).value_or(m_Projects.Root());
	}

	uint32 SourceManager::InFolder(const std::string& folder, const std::string& written) const
	{
		if (folder.empty()) return 0;
		auto it = m_AbsPathToId.find(NormalizePath((fs::path(folder) / written).lexically_normal().string()));
		return it != m_AbsPathToId.end() ? it->second : 0;
	}

	uint32 SourceManager::BesideRequester(const SourceInfo& requester, const std::string& written) const
	{
		if (!requester.absPath.empty()) return InFolder(NormalizePath(fs::path(requester.absPath).parent_path().string()), written);
		// A source registered in memory has a name, not a folder: the include is relative to that name's folder.
		const std::string candidate = NormalizePath((fs::path(requester.displayName).parent_path() / written).lexically_normal().string());
		auto it = m_NameToId.find(candidate);
		return it != m_NameToId.end() && m_Sources.at(it->second).byName ? it->second : 0;
	}

	std::vector<std::string> SourceManager::FoldersOf(uint32 project) const
	{
		std::vector<std::string> folders;
		for (const fs::path& folder : m_Projects.SearchFolders(project)) folders.push_back(NormalizePath(folder.lexically_normal().string()));
		for (const auto& [folder, owner] : m_Folders)
			if (owner == project && std::find(folders.begin(), folders.end(), folder) == folders.end()) folders.push_back(folder);
		return folders;
	}

	std::vector<std::string> SourceManager::FoldersReachedFrom(uint32 project) const
	{
		std::vector<std::string> folders;
		for (uint32 used : m_Projects.Uses(project))
			for (const std::string& folder : FoldersOf(used)) folders.push_back(folder);
		return folders;
	}

	uint32 SourceManager::InProject(uint32 project, const std::string& written) const
	{
		for (const std::string& folder : FoldersOf(project))
			if (uint32 id = InFolder(folder, written)) return id;
		auto it = m_NameToId.find(written);
		if (it == m_NameToId.end()) return 0;
		const SourceInfo& named = m_Sources.at(it->second);
		return named.byName && named.project == project ? it->second : 0;
	}

	IncludeTarget SourceManager::InDependencies(uint32 project, const std::string& written) const
	{
		uint32 found = 0, foundIn = 0;
		for (uint32 used : m_Projects.Uses(project))
		{
			if (used == project) continue;
			const uint32 id = InProject(used, written);
			if (!id || id == found) continue;
			if (found)
				return { 0, "'" + written + "' is a file of both '" + m_Projects.At(foundIn).name + "' (" + m_Sources.at(found).absPath +
					") and '" + m_Projects.At(used).name + "' (" + m_Sources.at(id).absPath + "): include it by a path only one of them has" };
			found = id;
			foundIn = used;
		}
		return { found };
	}

	std::string SourceManager::OutOfReach(uint32 project, const std::string& written) const
	{
		for (uint32 other = 0; other < m_Projects.Count(); ++other)
			if (!m_Projects.CanUse(project, other) && InProject(other, written))
				return "cannot include '" + written + "': it is a file of project '" + m_Projects.At(other).name + "', and project '" +
					m_Projects.At(project).name + "' does not depend on '" + m_Projects.At(other).name + "'";
		return "";
	}

	IncludeTarget SourceManager::ResolveInclude(const std::string& include, uint32 requester) const
	{
		const std::string written = NormalizePath(include);
		if (written.empty()) return {};
		const SourceInfo* from = HasInfo(requester) ? &GetInfo(requester) : nullptr;
		if (from)
			if (uint32 id = BesideRequester(*from, written)) return { id };
		if (from && from->library) return { InFolder(m_LibraryRoot, written) };
		const uint32 project = from ? from->project : m_Projects.Root();
		if (uint32 id = InProject(project, written)) return { id };
		IncludeTarget dependency = InDependencies(project, written);
		if (dependency.id || !dependency.error.empty()) return dependency;
		if (uint32 id = InFolder(m_LibraryRoot, written)) return { id };
		return { 0, OutOfReach(project, written) };
	}

} // namespace Scripting
