#include "Features/IncludeCompletion.h"
#include "Server/CompilationSession.h"
#include "SourceManager/SourceManager.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace C4LSP::Features
{
	namespace
	{
		constexpr int c_FileKind = 17, c_FolderKind = 19; // LSP CompletionItemKind

		bool IsIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
		bool IsSeparator(char c) { return c == '/' || c == '\\'; }

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return text;
		}

		fs::path PathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

		std::string Utf8(const fs::path& path)
		{
			const std::u8string text = path.generic_u8string();
			return std::string(text.begin(), text.end());
		}

		// A path as include lookups compare paths: normal, '/'-separated, and on Windows, whose names ignore case, lower case.
		std::string Key(const fs::path& path)
		{
			std::string text = Utf8(path.lexically_normal());
			while (text.size() > 1 && text.back() == '/')
				text.pop_back();
#ifdef _WIN32
			return Lower(text);
#else
			return text;
#endif
		}

		// A folder's own name ("Bedrock" of "C:/C4/Bedrock/").
		std::string NameOf(const fs::path& folder)
		{
			const fs::path normal = folder.lexically_normal();
			return Utf8(normal.has_filename() ? normal.filename() : normal.parent_path().filename());
		}

		bool StartsWithAnyCase(const std::string& name, const std::string& lowerPrefix)
		{
			return name.size() >= lowerPrefix.size() && Lower(name.substr(0, lowerPrefix.size())) == lowerPrefix;
		}

		// A file an include of this kind names: a C4 header, or for the C bindings a C or C++ one.
		bool IsHeader(const std::string& name, bool bindings)
		{
			const size_t dot = name.find_last_of('.');
			if (dot == std::string::npos || dot == 0)
				return false;
			const std::string extension = Lower(name.substr(dot));
			if (!bindings)
				return extension == ".ch";
			for (const char* header : { ".h", ".hh", ".hpp", ".hxx", ".h++", ".inl" })
				if (extension == header)
					return true;
			return false;
		}

		size_t SkipSpaceBack(const std::string& line, size_t at)
		{
			while (at > 0 && std::isspace(static_cast<unsigned char>(line[at - 1])))
				--at;
			return at;
		}

		// The identifier ending at `end`, and where it starts.
		std::string WordBefore(const std::string& line, size_t end, size_t& start)
		{
			start = end;
			while (start > 0 && IsIdentChar(line[start - 1]))
				--start;
			return line.substr(start, end - start);
		}

		// Whether `extern "c"` or `extern "c++"` ends at `end`: the include after it names a header for the C bindings.
		bool ExternBefore(const std::string& line, size_t end)
		{
			for (const std::string language : { "\"c\"", "\"c++\"" })
			{
				if (end < language.size() || line.compare(end - language.size(), language.size(), language) != 0)
					continue;
				size_t start = 0;
				return WordBefore(line, SkipSpaceBack(line, end - language.size()), start) == "extern";
			}
			return false;
		}
	} // namespace

	std::optional<IncludeCompletion::Site> IncludeCompletion::Find(const std::string& text, const Protocol::Position& position)
	{
		size_t lineStart = 0;
		for (uint32 line = 0; line < position.line; ++line)
		{
			const size_t newline = text.find('\n', lineStart);
			if (newline == std::string::npos)
				return std::nullopt;
			lineStart = newline + 1;
		}
		size_t lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string::npos)
			lineEnd = text.size();
		if (lineEnd > lineStart && text[lineEnd - 1] == '\r')
			--lineEnd;
		const std::string line = text.substr(lineStart, lineEnd - lineStart);
		const size_t caret = position.character;
		if (caret > line.size())
			return std::nullopt;

		// The literal the caret is in, if any: the line's literals in order, up to a comment.
		size_t open = std::string::npos;
		for (size_t i = 0; i < caret; ++i)
		{
			const char c = line[i];
			if (open != std::string::npos)
			{
				if (c == '\\')
					++i; // an escape: its character never closes the literal
				else if (c == '"')
					open = std::string::npos;
			}
			else if (c == '"')
				open = i;
			else if (c == '\'')
			{
				for (++i; i < caret && line[i] != '\''; ++i)
					if (line[i] == '\\')
						++i;
			}
			else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/')
				return std::nullopt; // the rest of the line is a comment
		}
		if (open == std::string::npos)
			return std::nullopt;
		size_t keywordStart = 0;
		if (WordBefore(line, SkipSpaceBack(line, open), keywordStart) != "include")
			return std::nullopt;

		Site site;
		site.line = position.line;
		site.bindings = ExternBefore(line, SkipSpaceBack(line, keywordStart));
		site.typed = line.substr(open + 1, caret - open - 1);
		size_t partStart = open + 1;
		for (size_t i = open + 1; i < caret; ++i)
			if (IsSeparator(line[i]))
				partStart = i + 1;
		size_t partEnd = caret;
		while (partEnd < line.size() && line[partEnd] != '"' && line[partEnd] != ';' && !IsSeparator(line[partEnd]) &&
			!std::isspace(static_cast<unsigned char>(line[partEnd])))
			++partEnd;
		site.partStart = static_cast<uint32>(partStart);
		site.partEnd = static_cast<uint32>(partEnd);
		site.slashFollows = partEnd < line.size() && IsSeparator(line[partEnd]);
		return site;
	}

	Core::Vector<CompletionItem> IncludeCompletion::Items(const Server::CompilationSession& session, const std::string& uri, const Site& site)
	{
		const Scripting::SourceManager& sources = session.GetSourceManager();
		const Scripting::ProjectSet& projects = sources.Projects();
		const fs::path file = PathOf(Protocol::DocumentUri::FromUri(uri)).lexically_normal();
		const std::string fileKey = Key(file);
		const fs::path library = sources.LibraryRoot().empty() ? fs::path() : PathOf(sources.LibraryRoot());

		// The folders an include of the file searches (docs/language/15-projects.md rule 3), nearest first, each with what an
		// item from it says it is from: nothing for the file's own project, else the project or library that has it.
		struct Root
		{
			fs::path folder;
			std::string from;
		};
		std::vector<Root> roots;
		std::set<std::string> rootKeys;
		std::set<std::string> sourceFolders; // the roots that are a project's or the library's own (not just the file's folder)
		const auto add = [&](const fs::path& folder, const std::string& from, bool sourceFolder) {
			if (folder.empty())
				return;
			const std::string key = Key(folder);
			if (sourceFolder)
				sourceFolders.insert(key);
			if (rootKeys.insert(key).second)
				roots.push_back({ folder.lexically_normal(), from });
		};
		add(file.parent_path(), "", false);
		const uint32 id = session.FindSourceIdByUri(uri);
		if (id && sources.GetInfo(id).library)
			add(library, "", true); // a library file's includes resolve in the library alone
		else
		{
			uint32 project = projects.Root();
			if (id)
				project = sources.GetInfo(id).project;
			else if (const auto owner = projects.OwnerOf(file))
				project = *owner;
			if (project < projects.Count())
				for (const uint32 used : projects.Uses(project))
				{
					const std::string from = used == project ? "" : projects.At(used).name;
					for (const fs::path& folder : projects.SearchFolders(used))
						add(folder, from, true);
					for (const auto& [folder, owner] : sources.Folders())
						if (owner == used)
							add(PathOf(folder), from, true);
				}
			if (!site.bindings)
				add(library, NameOf(library), true); // the C bindings read no C4 header
		}

		// The folders typed so far, and the start of the name being typed.
		const size_t cut = site.typed.find_last_of("/\\");
		const fs::path typedFolders = cut == std::string::npos ? fs::path() : PathOf(site.typed.substr(0, cut + 1));
		const std::string prefix = Lower(cut == std::string::npos ? site.typed : site.typed.substr(cut + 1));

		struct Entry
		{
			std::string name;
			bool folder = false;
			std::string from;
		};
		std::map<std::string, Entry> entries; // by the name as the file system compares names: the nearest folder's wins
		for (const Root& root : roots)
		{
			const fs::path folder = (root.folder / typedFolders).lexically_normal();
			const bool sourceFolder = sourceFolders.count(Key(folder)) != 0;
			std::error_code error;
			for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
			{
				const std::string name = Utf8(it->path().filename());
				if (name.empty() || name[0] == '.' || !StartsWithAnyCase(name, prefix))
					continue;
				std::error_code kindError;
				const bool isFolder = it->is_directory(kindError);
				if (isFolder)
				{
					// A source folder's own tests/ holds its project's tests, which only they may include (rule 7).
					if (sourceFolder && Lower(name) == "tests" && fileKey.rfind(Key(it->path()) + "/", 0) != 0)
						continue;
				}
				else if (!it->is_regular_file(kindError) || !IsHeader(name, site.bindings) || Key(it->path()) == fileKey)
					continue;
#ifdef _WIN32
				const std::string key = Lower(name);
#else
				const std::string key = name;
#endif
				entries.try_emplace(key, Entry{ name, isFolder, root.from });
			}
		}

		std::vector<const Entry*> sorted;
		for (const auto& [key, entry] : entries)
			sorted.push_back(&entry);
		std::stable_sort(sorted.begin(), sorted.end(), [](const Entry* a, const Entry* b) { return Lower(a->name) < Lower(b->name); });

		Core::Vector<CompletionItem> items;
		for (size_t i = 0; i < sorted.size(); ++i)
		{
			const Entry& entry = *sorted[i];
			CompletionItem item;
			item.kind = entry.folder ? c_FolderKind : c_FileKind;
			item.label = entry.folder ? entry.name + "/" : entry.name;
			item.detail = entry.from;
			item.edit = Protocol::Range{ { site.line, site.partStart }, { site.line, site.partEnd } };
			// A folder brings its '/' and asks for what is in it, unless the path already goes on into it.
			item.newText = entry.folder && !site.slashFollows ? entry.name + "/" : entry.name;
			item.completeAgain = entry.folder && !site.slashFollows;
			char order[16];
			std::snprintf(order, sizeof(order), "%06zu", i);
			item.sortText = order;
			items.Push(item);
		}
		return items;
	}
} // namespace C4LSP::Features
