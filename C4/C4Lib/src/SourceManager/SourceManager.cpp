#include "SourceManager.h"
#include "Compiler/FileIo.h"
#include "Compiler/ParallelFor.h"
#include <algorithm>
#include <thread>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace Scripting
{
	namespace
	{
		// A path as UTF-8, C4's text: path::string() converts to the ANSI code page on Windows and throws for a
		// name that page cannot hold (BUG-300).
		std::string Utf8(const fs::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		fs::path PathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

		bool IsC4Source(const fs::path& path)
		{
			const fs::path extension = path.extension();
			return extension == ".cf" || extension == ".ch";
		}
	}

	uint32 SourceManager::RegisterFile(const fs::path& path)
	{
		if (!IsFile(Utf8(path))) return 0;

		std::string absPath = NormalizePath(Utf8(fs::absolute(path).lexically_normal()));
		if (m_AbsPaths.count(absPath)) return 0;

		uint32 id = m_NextId++;
		m_AbsPaths.insert(absPath);

		std::string name = Utf8(path.filename());
		SourceInfo info{ id, name, NormalizePath(Utf8(fs::absolute(path).parent_path())), absPath, CreateRef<Source>(path) };
		info.project = ProjectOf(absPath);
		info.byName = true;
		m_Sources.emplace(id, std::move(info));
		m_NameToId[name] = id;
		m_AbsPathToId[absPath] = id;

		return id;
	}

	uint32 SourceManager::RegisterInMemory(const fs::path& name, const std::string& sourceText)
	{
		std::string nameStr = Utf8(name.filename());
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
		SourceInfo info{ id, displayName, NormalizePath(Utf8(PathOf(path).parent_path())), path, Source::FromMemory(text) };
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

	// Walked by its extended spelling (FileIo's ExtendedPath), so a deep folder lists (BUG-356); its entries are named
	// as they read.
	uint32 SourceManager::RegisterFolder(const fs::path& folder, FolderScan scan)
	{
		if (!IsFolder(Utf8(folder))) return 0;

		std::string baseDir = NormalizePath(Utf8(fs::absolute(folder).lexically_normal()));
		while (baseDir.size() > 1 && baseDir.back() == '/') baseDir.pop_back();
		const uint32 project = ProjectOf(baseDir);
		if (std::find(m_Folders.begin(), m_Folders.end(), std::make_pair(baseDir, project)) == m_Folders.end())
			m_Folders.push_back({ baseDir, project });

		// Error codes, never exceptions: a folder that cannot be read is skipped, as the build skips it.
		std::error_code error;
		const auto options = fs::directory_options::skip_permission_denied;
		const fs::path walked = ExtendedPath(folder);
		const fs::path plainFolder = PlainPath(walked);
		std::vector<fs::path> found;
		auto take = [&](const fs::directory_entry& entry) {
			std::error_code kindError;
			if (!entry.is_regular_file(kindError) || !IsC4Source(entry.path())) return;
			const fs::path path = PlainPath(entry.path());
			if (!m_AbsPaths.count(NormalizePath(Utf8(path)))) found.push_back(path);
		};
		if (scan == FolderScan::TopOnly)
			for (fs::directory_iterator it(walked, options, error), end; !error && it != end; it.increment(error)) take(*it);
		else
			for (fs::recursive_directory_iterator it(walked, options, error), end; !error && it != end; it.increment(error)) take(*it);

		// The files are read on several threads, then registered in the order the walk found them.
		std::vector<Ref<Source>> read(found.size());
		ParallelFor(found.size(), std::thread::hardware_concurrency(), [&](size_t i) { read[i] = CreateRef<Source>(found[i]); });
		uint32 count = 0;
		for (size_t i = 0; i < found.size(); ++i) count += RegisterFolderFile(found[i], read[i], plainFolder, baseDir);
		return count;
	}

	uint32 SourceManager::RegisterFolderFile(const fs::path& path, Ref<Source> source, const fs::path& folder, const std::string& baseDir)
	{
		std::string absPath = NormalizePath(Utf8(path));
		if (m_AbsPaths.count(absPath)) return 0;

		uint32 id = m_NextId++;
		m_AbsPaths.insert(absPath);

		std::string relStr = NormalizePath(Utf8(path.lexically_relative(folder)));

		SourceInfo info{ id, relStr, baseDir, absPath, std::move(source) };
		info.project = ProjectOf(absPath);
		m_Sources.emplace(id, std::move(info));
		m_NameToId[relStr] = id;
		m_AbsPathToId[absPath] = id;
		return 1;
	}

	namespace
	{
		size_t SkipSpaceBack(const std::string& text, size_t end)
		{
			while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
			return end;
		}

		// Whether the `include` at `at` is a foreign one, `extern "c" include "x.h"`: a C or C++ header for the interop
		// pass to bind, never a C4 file of the program or the library (BUG-392).
		bool IsForeignInclude(const std::string& text, size_t at)
		{
			size_t end = SkipSpaceBack(text, at);
			if (end < 2 || text[end - 1] != '"') return false;
			const size_t open = text.rfind('"', end - 2);
			if (open == std::string::npos) return false;
			end = SkipSpaceBack(text, open);
			const std::string keyword = "extern";
			if (end < keyword.size() || text.compare(end - keyword.size(), keyword.size(), keyword) != 0) return false;
			const size_t start = end - keyword.size();
			return start == 0 || (!std::isalnum(static_cast<unsigned char>(text[start - 1])) && text[start - 1] != '_');
		}

		// The `include "..."` targets written in a source text (commented-out lines and foreign includes skipped).
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
				if (!wordStart || commentedOut || IsForeignInclude(text, i)) { pos = i + 1; continue; }
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

	uint32 SourceManager::RegisterIncludedLibraryFiles(const fs::path& rootPath, const std::vector<fs::path>& opened)
	{
		if (!IsFolder(Utf8(rootPath))) return 0;
		std::string root = NormalizePath(Utf8(fs::absolute(rootPath).lexically_normal()));
		std::vector<fs::path> entries;
		for (const fs::path& file : opened)
			if (IsFile(Utf8(file))) entries.push_back(file);
		std::map<std::string, bool> exists; // the file system asked once per path: most files include the same headers
		for (const auto& [id, info] : m_Sources)
			for (const std::string& inc : ExtractIncludes(info.source->Data()))
			{
				if (ResolveIncludePath(inc.c_str(), id) != 0) continue; // the program's own file
				fs::path candidate = PathOf(root) / PathOf(inc);
				auto [asked, fresh] = exists.try_emplace(Utf8(candidate));
				if (fresh) asked->second = IsFile(asked->first);
				if (asked->second) entries.push_back(candidate); // every time: the walk takes them from the back
			}
		const fs::path prelude = PathOf(root) / "Prelude" / "prelude.ch";
		const bool hasPrelude = IsFile(Utf8(prelude));
		if (entries.empty() && !hasPrelude) return 0;
		m_LibraryRoot = root;
		uint32 count = RegisterLibraryClosure(root, std::move(entries));
		if (!hasPrelude) return count;
		count += RegisterLibraryClosure(root, { prelude });
		m_PreludeId = m_AbsPathToId.at(NormalizePath(Utf8(fs::absolute(prelude))));
		return count;
	}

	uint32 SourceManager::RegisterLibraryClosure(const std::string& root, std::vector<fs::path> queue)
	{
		std::set<std::string> queued;
		uint32 count = 0;
		while (!queue.empty()) {
			fs::path cur = queue.back(); queue.pop_back();
			std::string absCur = NormalizePath(Utf8(fs::absolute(cur).lexically_normal()));
			if (queued.count(absCur) || m_AbsPaths.count(absCur)) continue;
			queued.insert(absCur);

			uint32 id = m_NextId++;
			m_AbsPaths.insert(absCur);
			std::string relStr = NormalizePath(Utf8(fs::absolute(cur).lexically_normal().lexically_relative(PathOf(root))));
			SourceInfo info{ id, relStr, NormalizePath(Utf8(fs::absolute(cur).parent_path())), absCur, CreateRef<Source>(cur) };
			info.project = m_Projects.Root();
			info.library = true;
			m_Sources.emplace(id, std::move(info));
			m_NameToId[relStr] = id;
			m_AbsPathToId[absCur] = id;
			++count;

			// Register the paired implementation file (.ch <-> .cf) in the same directory.
			if (IsC4Source(cur)) {
				fs::path paired = cur;
				paired.replace_extension(cur.extension() == ".ch" ? ".cf" : ".ch");
				if (IsFile(Utf8(paired))) queue.push_back(paired);
			}

			// A file this walk registered exists and is not registered again: most includes name one, and asking the file
			// system for each costs more than the rest of the walk.
			auto known = [&](const fs::path& p) { return queued.count(NormalizePath(Utf8(p.lexically_normal()))) != 0; };
			for (const auto& inc : ExtractIncludes(m_Sources.at(id).source->Data())) {
				fs::path c1 = fs::absolute(cur.parent_path() / PathOf(inc));
				if (known(c1)) continue;
				if (IsFile(Utf8(c1))) { queue.push_back(c1); continue; }
				fs::path c2 = fs::absolute(PathOf(root) / PathOf(inc));
				if (!known(c2) && IsFile(Utf8(c2))) queue.push_back(c2);
			}
		}
		return count;
	}

	uint32 SourceManager::FindByAbsPath(const std::string& path) const
	{
		const std::string wanted = NormalizePath(Utf8(PathOf(path).lexically_normal()));
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
		return m_Projects.OwnerOf(PathOf(absPath)).value_or(m_Projects.Root());
	}

	uint32 SourceManager::InFolder(const std::string& folder, const std::string& written) const
	{
		if (folder.empty()) return 0;
		auto it = m_AbsPathToId.find(NormalizePath(Utf8((PathOf(folder) / PathOf(written)).lexically_normal())));
		return it != m_AbsPathToId.end() ? it->second : 0;
	}

	uint32 SourceManager::BesideRequester(const SourceInfo& requester, const std::string& written) const
	{
		if (!requester.absPath.empty()) return InFolder(NormalizePath(Utf8(PathOf(requester.absPath).parent_path())), written);
		// A source registered in memory has a name, not a folder: the include is relative to that name's folder.
		const std::string candidate = NormalizePath(Utf8((PathOf(requester.displayName).parent_path() / PathOf(written)).lexically_normal()));
		auto it = m_NameToId.find(candidate);
		return it != m_NameToId.end() && m_Sources.at(it->second).byName ? it->second : 0;
	}

	std::vector<std::string> SourceManager::FoldersOf(uint32 project) const
	{
		std::vector<std::string> folders;
		for (const fs::path& folder : m_Projects.SearchFolders(project)) folders.push_back(NormalizePath(Utf8(folder.lexically_normal())));
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

	IncludeTarget SourceManager::InProject(uint32 project, const std::string& written) const
	{
		uint32 found = 0;
		for (const std::string& folder : FoldersOf(project))
		{
			const uint32 id = InFolder(folder, written);
			if (!id || id == found) continue;
			if (found)
				return { 0, "'" + written + "' is in two source folders of project '" + m_Projects.At(project).name + "' (" +
					m_Sources.at(found).absPath + " and " + m_Sources.at(id).absPath + "): rename one, or include it by a path only one of them has" };
			found = id;
		}
		if (found) return { found };
		auto it = m_NameToId.find(written);
		if (it == m_NameToId.end()) return {};
		const SourceInfo& named = m_Sources.at(it->second);
		return { named.byName && named.project == project ? it->second : 0 };
	}

	IncludeTarget SourceManager::InDependencies(uint32 project, const std::string& written) const
	{
		uint32 found = 0, foundIn = 0;
		for (uint32 used : m_Projects.Uses(project))
		{
			if (used == project) continue;
			const IncludeTarget in = InProject(used, written);
			if (!in.error.empty()) return in;
			const uint32 id = in.id;
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
		{
			if (m_Projects.CanUse(project, other)) continue;
			const IncludeTarget in = InProject(other, written);
			if (in.id || !in.error.empty())
				return "cannot include '" + written + "': it is a file of project '" + m_Projects.At(other).name + "', and project '" +
					m_Projects.At(project).name + "' does not depend on '" + m_Projects.At(other).name + "'";
		}
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
		if (IncludeTarget own = InProject(project, written); own.id || !own.error.empty()) return own;
		IncludeTarget dependency = InDependencies(project, written);
		if (dependency.id || !dependency.error.empty()) return dependency;
		if (uint32 id = InFolder(m_LibraryRoot, written)) return { id };
		return { 0, OutOfReach(project, written) };
	}

} // namespace Scripting
