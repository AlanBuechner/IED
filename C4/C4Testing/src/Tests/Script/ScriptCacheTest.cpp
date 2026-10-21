#include "ScriptCacheTest.h"
#include "Script/ScriptCache.h"
#include "Script/ScriptCompiler.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Testing
{
	using namespace Scripting;
	namespace fs = std::filesystem;

	namespace
	{
		fs::path Folder(const char* name)
		{
			const fs::path folder = fs::temp_directory_path() / "c4-scriptcache-test" / name;
			std::error_code ec;
			fs::remove_all(folder, ec);
			return folder;
		}

		ScriptCompileOptions Options(const char* name, const char* text)
		{
			ScriptCompileOptions options;
			options.name = name;
			options.files.push_back({ "main.cf", std::string(text) });
			return options;
		}

		HostInterface Host(uint8 build)
		{
			HostInterface host;
			host.buildId.fill(build);
			return host;
		}

		CachedScript Script(std::string object)
		{
			CachedScript script;
			script.object = std::move(object);
			script.diagnostics = "main.cf (1:1) warning: kept\n";
			script.threadLocals = { "t_one", "t_two" };
			return script;
		}

		std::vector<std::string> EntriesIn(const fs::path& folder)
		{
			std::vector<std::string> names;
			std::error_code ec;
			for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) names.push_back(entry.path().filename().string());
			std::sort(names.begin(), names.end());
			return names;
		}

		bool StartsWith(const std::string& text, const std::string& start) { return text.rfind(start, 0) == 0; }
	}

	void ScriptCacheTest::TheSameInputsNameTheSameEntry()
	{
		const ScriptCache cache(Folder("same").string(), "compiler");
		const std::string entry = cache.EntryFor(Host(1), Options("game", "int32 F() { return 1; }"));
		TEST_ASSERT(!entry.empty());
		TEST_ASSERT(entry == cache.EntryFor(Host(1), Options("game", "int32 F() { return 1; }")));
		TEST_PASSED();
	}

	void ScriptCacheTest::EveryInputChangesTheEntry()
	{
		const ScriptCache cache(Folder("inputs").string(), "compiler");
		const ScriptCompileOptions base = Options("game", "int32 F() { return 1; }");
		const std::string entry = cache.EntryFor(Host(1), base);
		TEST_ASSERT(entry != cache.EntryFor(Host(1), Options("game", "int32 F() { return 2; }")));
		TEST_ASSERT(entry != cache.EntryFor(Host(2), base));
		TEST_ASSERT(entry != ScriptCache(Folder("inputs").string(), "another compiler").EntryFor(Host(1), base));
		ScriptCompileOptions defined = base;
		defined.defines["MODE"] = "2";
		TEST_ASSERT(entry != cache.EntryFor(Host(1), defined));
		ScriptCompileOptions allowed = base;
		allowed.allow.push_back("api/");
		TEST_ASSERT(entry != cache.EntryFor(Host(1), allowed));
		ScriptCompileOptions renamedFile = base;
		renamedFile.files[0].path = "other.cf";
		TEST_ASSERT(entry != cache.EntryFor(Host(1), renamedFile));
		TEST_PASSED();
	}

	void ScriptCacheTest::AnEntryIsNamedForItsScriptAndLevel()
	{
		const fs::path folder = Folder("names");
		const ScriptCache cache(folder.string(), "compiler");
		ScriptCompileOptions options = Options("plugins/go plugin", "int32 F() { return 1; }");
		const fs::path optimized(cache.EntryFor(Host(1), options));
		options.optimize = false;
		const fs::path plain(cache.EntryFor(Host(1), options));
		TEST_ASSERT(optimized.parent_path() == folder);
		TEST_ASSERT(StartsWith(optimized.filename().string(), "plugins_go_plugin.O2-"));
		TEST_ASSERT(StartsWith(plain.filename().string(), "plugins_go_plugin.O0-"));
		TEST_ASSERT(optimized.extension() == ".c4so");
		TEST_PASSED();
	}

	void ScriptCacheTest::AnEntryReadsBackWhole()
	{
		const ScriptCache cache(Folder("roundtrip").string(), "compiler");
		const std::string entry = cache.EntryFor(Host(1), Options("game", "int32 F() { return 1; }"));
		const std::string object("MZ object\0bytes", 15); // machine code holds zeros
		cache.Write(entry, Script(object));
		std::optional<CachedScript> read = cache.Read(entry);
		TEST_ASSERT(read.has_value());
		TEST_ASSERT(read->object == object);
		TEST_ASSERT(read->diagnostics == "main.cf (1:1) warning: kept\n");
		TEST_ASSERT(read->threadLocals == std::vector<std::string>({ "t_one", "t_two" }));
		TEST_PASSED();
	}

	void ScriptCacheTest::ADamagedOrMissingEntryReadsAsNone()
	{
		const ScriptCache cache(Folder("damaged").string(), "compiler");
		const std::string entry = cache.EntryFor(Host(1), Options("game", "int32 F() { return 1; }"));
		TEST_ASSERT(!cache.Read(entry).has_value());
		cache.Write(entry, Script("object"));
		const auto size = fs::file_size(entry);
		fs::resize_file(entry, size - 3);
		TEST_ASSERT(!cache.Read(entry).has_value());
		{
			std::ofstream junk(entry, std::ios::binary | std::ios::trunc);
			junk << "junk";
		}
		TEST_ASSERT(!cache.Read(entry).has_value());
		TEST_PASSED();
	}

	void ScriptCacheTest::AWriteReplacesOnlyItsScriptsEntryAtItsLevel()
	{
		const fs::path folder = Folder("replace");
		const ScriptCache cache(folder.string(), "compiler");
		ScriptCompileOptions first = Options("game", "int32 F() { return 1; }");
		ScriptCompileOptions plain = first;
		plain.optimize = false;
		const std::string other = cache.EntryFor(Host(1), Options("game-ui", "int32 G() { return 1; }"));
		cache.Write(cache.EntryFor(Host(1), first), Script("first"));
		cache.Write(cache.EntryFor(Host(1), plain), Script("plain"));
		cache.Write(other, Script("other"));
		const std::string second = cache.EntryFor(Host(1), Options("game", "int32 F() { return 2; }"));
		cache.Write(second, Script("second"));
		const std::vector<std::string> left = EntriesIn(folder);
		TEST_ASSERT(left.size() == 3);
		TEST_ASSERT(std::find(left.begin(), left.end(), fs::path(second).filename().string()) != left.end());
		TEST_ASSERT(std::find(left.begin(), left.end(), fs::path(other).filename().string()) != left.end());
		TEST_ASSERT(std::find(left.begin(), left.end(), fs::path(cache.EntryFor(Host(1), plain)).filename().string()) != left.end());
		TEST_PASSED();
	}

	// A host tells builds of one script apart by a trailing `#<tag>`; each build hits the same entry.
	void ScriptCacheTest::ABuildTagSharesTheEntry()
	{
		const ScriptCache cache(Folder("tags").string(), "compiler");
		const std::string first = cache.EntryFor(Host(1), Options("go@ws1#1", "int32 F() { return 1; }"));
		TEST_ASSERT(first == cache.EntryFor(Host(1), Options("go@ws1#2", "int32 F() { return 1; }")));
		TEST_ASSERT(first == cache.EntryFor(Host(1), Options("go@ws1", "int32 F() { return 1; }")));
		TEST_ASSERT(first != cache.EntryFor(Host(1), Options("go@ws2#1", "int32 F() { return 1; }")));
		TEST_ASSERT(StartsWith(fs::path(first).filename().string(), "go_ws1.O2-"));
		TEST_PASSED();
	}

	void ScriptCacheTest::AnUnreadableScriptFileHasNoEntry()
	{
		const ScriptCache cache(Folder("unreadable").string(), "compiler");
		ScriptCompileOptions options;
		options.name = "game";
		options.files.push_back({ (Folder("unreadable") / "missing.cf").string(), std::nullopt });
		TEST_ASSERT(cache.EntryFor(Host(1), options).empty());
		TEST_PASSED();
	}
} // namespace Testing
