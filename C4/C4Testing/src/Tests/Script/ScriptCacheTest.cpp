#include "ScriptCacheTest.h"
#include "Script/ScriptCache.h"
#include "Script/ScriptCompiler.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>

namespace Testing
{
	using namespace C4Compiler;
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

		// Projects Engine, Tools (on Engine) and the root Game (on both), a file each of Engine and Tools.
		HostInterface Interface(const std::string& engine = "int32 F() ;\nint32 G() ;\n", const std::string& tools = "int32 H() ;\n")
		{
			HostInterface host;
			host.projects = { { "Engine", {}, {} }, { "Tools", { 0 }, {} }, { "Game", { 0, 1 }, {} } };
			host.rootProject = 2;
			host.files.resize(2);
			host.files[0].unitId = 1;
			host.files[0].project = 0;
			host.files[0].path = "engine.ch";
			host.files[0].text = engine;
			host.files[1].unitId = 2;
			host.files[1].project = 1;
			host.files[1].path = "tools.ch";
			host.files[1].text = tools;
			return host;
		}

		// An interface with the digests of its files, as a script host keeps them.
		struct CacheHost
		{
			HostInterface host;
			InterfaceDigests digests{ host };
		};

		std::unique_ptr<CacheHost> Made(HostInterface host)
		{
			auto made = std::make_unique<CacheHost>();
			made->host = std::move(host);
			return made;
		}

		std::string EntryOf(const ScriptCache& cache, HostInterface host, const ScriptCompileOptions& options)
		{
			std::unique_ptr<CacheHost> made = Made(std::move(host));
			return cache.EntryFor(made->host, made->digests, options);
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

		ScriptCompileOptions OnEngine(const char* text)
		{
			ScriptCompileOptions options = Options("game", text);
			options.allow = { "Engine" };
			options.onlyAllowed = true;
			return options;
		}
	}

	void ScriptCacheTest::TheSameInputsNameTheSameEntry()
	{
		const ScriptCache cache(Folder("same").string(), "compiler");
		const std::string entry = EntryOf(cache, Interface(), Options("game", "int32 F() { return 1; }"));
		TEST_ASSERT(!entry.empty());
		TEST_ASSERT(entry == EntryOf(cache, Interface(), Options("game", "int32 F() { return 1; }")));
		TEST_PASSED();
	}

	void ScriptCacheTest::EveryInputChangesTheEntry()
	{
		const ScriptCache cache(Folder("inputs").string(), "compiler");
		const ScriptCompileOptions base = Options("game", "int32 F() { return 1; }");
		const std::string entry = EntryOf(cache, Interface(), base);
		TEST_ASSERT(entry != EntryOf(cache, Interface(), Options("game", "int32 F() { return 2; }")));
		TEST_ASSERT(entry != EntryOf(ScriptCache(Folder("inputs").string(), "another compiler"), Interface(), base));
		ScriptCompileOptions defined = base;
		defined.defines["MODE"] = "2";
		TEST_ASSERT(entry != EntryOf(cache, Interface(), defined));
		ScriptCompileOptions allowed = base;
		allowed.allow.push_back("api/");
		TEST_ASSERT(entry != EntryOf(cache, Interface(), allowed));
		ScriptCompileOptions only = base;
		only.onlyAllowed = true;
		TEST_ASSERT(entry != EntryOf(cache, Interface(), only));
		ScriptCompileOptions renamedFile = base;
		renamedFile.files[0].path = "other.cf";
		TEST_ASSERT(entry != EntryOf(cache, Interface(), renamedFile));
		HostInterface target = Interface();
		target.targetFeatures = "+avx2";
		TEST_ASSERT(entry != EntryOf(cache, target, base));
		TEST_PASSED();
	}

	// BUG-461: a host built again - another build id, other symbols and inline bodies, which the entry's assumptions check
	// - with the same files keeps the entry.
	void ScriptCacheTest::AHostBuildOfTheSameFilesKeepsTheEntry()
	{
		const ScriptCache cache(Folder("rebuilt").string(), "compiler");
		const ScriptCompileOptions options = Options("game", "int32 F() { return 1; }");
		HostInterface rebuilt = Interface();
		rebuilt.buildId.fill(7);
		rebuilt.symbols = { "F_0", "Plan_0" };
		rebuilt.inlineBodies = { "BC" };
		TEST_ASSERT(EntryOf(cache, Interface(), options) == EntryOf(cache, rebuilt, options));
		TEST_PASSED();
	}

	// A body edited leaves its file's interface text but for its line breaks: the entry stays.
	void ScriptCacheTest::ABodyEditKeepsTheEntry()
	{
		const ScriptCache cache(Folder("body").string(), "compiler");
		const ScriptCompileOptions options = Options("game", "int32 F() { return 1; }");
		TEST_ASSERT(EntryOf(cache, Interface("int32 F() ;\nint32 G() ;\n"), options) ==
			EntryOf(cache, Interface("int32 F() ;\n\n\n  \nint32 G() ;\t\n"), options));
		TEST_ASSERT(EntryOf(cache, Interface("int32 F() ;\nint32 G() ;\n"), options) !=
			EntryOf(cache, Interface("int64 F() ;\nint32 G() ;\n"), options));
		TEST_ASSERT(EntryOf(cache, Interface("int32 F() ;\n"), options) != EntryOf(cache, Interface("int32  F() ;\n"), options));
		TEST_PASSED();
	}

	// An assertion records its line: one that moves changes the entry, though only blank lines came before it.
	void ScriptCacheTest::AnAssertionThatMovesChangesTheEntry()
	{
		const ScriptCache cache(Folder("assert").string(), "compiler");
		const ScriptCompileOptions options = Options("game", "int32 F() { return 1; }");
		const std::string at3 = "int32 F() ;\n\ntemplate<T> void Check(T v) { assert_true(v, \"v\"); }\n";
		const std::string at4 = "int32 F() ;\n\n\ntemplate<T> void Check(T v) { assert_true(v, \"v\"); }\n";
		TEST_ASSERT(EntryOf(cache, Interface(at3), options) != EntryOf(cache, Interface(at4), options));
		TEST_PASSED();
	}

	// A script whose project depends on Engine alone reads nothing of Tools: Tools' files are out of its entry.
	void ScriptCacheTest::AFileOutOfReachKeepsTheEntry()
	{
		const ScriptCache cache(Folder("reach").string(), "compiler");
		const ScriptCompileOptions options = OnEngine("int32 F() { return 1; }");
		const std::string entry = EntryOf(cache, Interface(), options);
		TEST_ASSERT(entry == EntryOf(cache, Interface("int32 F() ;\nint32 G() ;\n", "int64 H() ;\nint32 I() ;\n"), options));
		TEST_ASSERT(entry != EntryOf(cache, Interface("int64 F() ;\nint32 G() ;\n"), options));
		const ScriptCompileOptions everything = Options("game", "int32 F() { return 1; }"); // no list: it reaches Tools too
		TEST_ASSERT(EntryOf(cache, Interface(), everything) != EntryOf(cache, Interface("int32 F() ;\nint32 G() ;\n", "int64 H() ;\n"), everything));
		TEST_PASSED();
	}

	// A file more moves the ids of the script's own units: every entry goes.
	void ScriptCacheTest::AnAddedFileChangesTheEntry()
	{
		const ScriptCache cache(Folder("added").string(), "compiler");
		const ScriptCompileOptions options = OnEngine("int32 F() { return 1; }");
		HostInterface more = Interface();
		InterfaceFile added = more.files[1];
		added.unitId = 3;
		added.path = "more.ch";
		more.files.push_back(added);
		TEST_ASSERT(EntryOf(cache, Interface(), options) != EntryOf(cache, more, options));
		TEST_PASSED();
	}

	// Which function has a collided symbol follows from the order of every file's functions: a new collision changes it.
	void ScriptCacheTest::ACollidedSymbolChangesTheEntry()
	{
		const ScriptCache cache(Folder("collided").string(), "compiler");
		const ScriptCompileOptions options = OnEngine("int32 F() { return 1; }");
		HostInterface collided = Interface();
		collided.collidedSymbols = { { "F_0", { 1, 2 } } };
		TEST_ASSERT(EntryOf(cache, Interface(), options) != EntryOf(cache, collided, options));
		TEST_PASSED();
	}

	void ScriptCacheTest::AnEntryIsNamedForItsScriptAndLevel()
	{
		const fs::path folder = Folder("names");
		const ScriptCache cache(folder.string(), "compiler");
		ScriptCompileOptions options = Options("plugins/go plugin", "int32 F() { return 1; }");
		const fs::path optimized(EntryOf(cache, Interface(), options));
		options.optimize = false;
		const fs::path plain(EntryOf(cache, Interface(), options));
		TEST_ASSERT(optimized.parent_path() == folder);
		TEST_ASSERT(StartsWith(optimized.filename().string(), "plugins_go_plugin.O2-"));
		TEST_ASSERT(StartsWith(plain.filename().string(), "plugins_go_plugin.O0-"));
		TEST_ASSERT(optimized.extension() == ".c4so");
		TEST_PASSED();
	}

	void ScriptCacheTest::AnEntryReadsBackWhole()
	{
		const ScriptCache cache(Folder("roundtrip").string(), "compiler");
		const std::string entry = EntryOf(cache, Interface(), Options("game", "int32 F() { return 1; }"));
		const std::string object("MZ object\0bytes", 15); // machine code holds zeros
		CachedScript written = Script(object);
		written.assumptions.symbols = { { "F_0", true }, { "Box_int32_Get_0", false } };
		written.assumptions.types = { { "Game::Box", false } };
		written.assumptions.inlineBodies = { { "Core_Min_0", "0123abcd" } };
		written.assumptions.wholeInterface = "fedc";
		cache.Write(entry, written);
		std::optional<CachedScript> read = cache.Read(entry);
		TEST_ASSERT(read.has_value());
		TEST_ASSERT(read->object == object);
		TEST_ASSERT(read->diagnostics == "main.cf (1:1) warning: kept\n");
		TEST_ASSERT(read->threadLocals == std::vector<std::string>({ "t_one", "t_two" }));
		TEST_ASSERT(read->assumptions.symbols == written.assumptions.symbols);
		TEST_ASSERT(read->assumptions.types == written.assumptions.types);
		TEST_ASSERT(read->assumptions.inlineBodies == written.assumptions.inlineBodies);
		TEST_ASSERT(read->assumptions.wholeInterface == "fedc");
		TEST_PASSED();
	}

	void ScriptCacheTest::ADamagedOrMissingEntryReadsAsNone()
	{
		const ScriptCache cache(Folder("damaged").string(), "compiler");
		const std::string entry = EntryOf(cache, Interface(), Options("game", "int32 F() { return 1; }"));
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
		const std::string other = EntryOf(cache, Interface(), Options("game-ui", "int32 G() { return 1; }"));
		cache.Write(EntryOf(cache, Interface(), first), Script("first"));
		cache.Write(EntryOf(cache, Interface(), plain), Script("plain"));
		cache.Write(other, Script("other"));
		const std::string second = EntryOf(cache, Interface(), Options("game", "int32 F() { return 2; }"));
		cache.Write(second, Script("second"));
		const std::vector<std::string> left = EntriesIn(folder);
		TEST_ASSERT(left.size() == 3);
		TEST_ASSERT(std::find(left.begin(), left.end(), fs::path(second).filename().string()) != left.end());
		TEST_ASSERT(std::find(left.begin(), left.end(), fs::path(other).filename().string()) != left.end());
		TEST_ASSERT(std::find(left.begin(), left.end(), fs::path(EntryOf(cache, Interface(), plain)).filename().string()) != left.end());
		TEST_PASSED();
	}

	// A host tells builds of one script apart by a trailing `#<tag>`; each build hits the same entry.
	void ScriptCacheTest::ABuildTagSharesTheEntry()
	{
		const ScriptCache cache(Folder("tags").string(), "compiler");
		const std::string first = EntryOf(cache, Interface(), Options("go@ws1#1", "int32 F() { return 1; }"));
		TEST_ASSERT(first == EntryOf(cache, Interface(), Options("go@ws1#2", "int32 F() { return 1; }")));
		TEST_ASSERT(first == EntryOf(cache, Interface(), Options("go@ws1", "int32 F() { return 1; }")));
		TEST_ASSERT(first != EntryOf(cache, Interface(), Options("go@ws2#1", "int32 F() { return 1; }")));
		TEST_ASSERT(StartsWith(fs::path(first).filename().string(), "go_ws1.O2-"));
		TEST_PASSED();
	}

	void ScriptCacheTest::AnUnreadableScriptFileHasNoEntry()
	{
		const ScriptCache cache(Folder("unreadable").string(), "compiler");
		ScriptCompileOptions options;
		options.name = "game";
		options.files.push_back({ (Folder("unreadable") / "missing.cf").string(), std::nullopt });
		TEST_ASSERT(EntryOf(cache, Interface(), options).empty());
		TEST_PASSED();
	}

	// What a cached script's code assumed of its host's names holds while the host answers alike.
	void ScriptCacheTest::AssumptionsHoldWhileTheHostAnswersAlike()
	{
		HostInterface host = Interface();
		host.symbols = { "F_0" };
		host.reflectedTypes = { "Engine::Crate" };
		std::unique_ptr<CacheHost> made = Made(host);
		const HostIndex index(made->host);
		HostAssumptions assumptions;
		assumptions.symbols = { { "F_0", true }, { "Plan_0", false } };
		assumptions.types = { { "Engine::Crate", true } };
		TEST_ASSERT(AssumptionsHold(assumptions, index, {}, made->digests));

		HostInterface defines = host;
		defines.symbols.push_back("Plan_0"); // the host makes what the script made for itself
		std::unique_ptr<CacheHost> other = Made(defines);
		TEST_ASSERT(!AssumptionsHold(assumptions, HostIndex(other->host), {}, other->digests));
		HostInterface untyped = host;
		untyped.reflectedTypes.clear();
		std::unique_ptr<CacheHost> third = Made(untyped);
		TEST_ASSERT(!AssumptionsHold(assumptions, HostIndex(third->host), {}, third->digests));

		assumptions.wholeInterface = made->digests.Whole(); // a compile that read every file holds while every file is the same
		TEST_ASSERT(AssumptionsHold(assumptions, index, {}, made->digests));
		std::unique_ptr<CacheHost> edited = Made(Interface("int32 F() ;\nint32 G() ;\n", "int64 H() ;\n"));
		TEST_ASSERT(!AssumptionsHold(assumptions, HostIndex(edited->host), {}, edited->digests));
		TEST_PASSED();
	}

	void ScriptCacheTest::AFilesDigestIsItsTextWithoutBlankLines()
	{
		TEST_ASSERT(InterfaceTextDigest("int32 F() ;\nint32 G() ;\n") == InterfaceTextDigest("int32 F() ;\r\n\n  \t\nint32 G() ;  \n\n"));
		TEST_ASSERT(InterfaceTextDigest("int32 F() ;\n") != InterfaceTextDigest("int32 F () ;\n"));
		TEST_ASSERT(InterfaceTextDigest("assert_true(x);\n") != InterfaceTextDigest("\nassert_true(x);\n"));
		TEST_ASSERT(InterfaceTextDigest("") == InterfaceTextDigest("\n\n"));
		TEST_PASSED();
	}
} // namespace Testing
