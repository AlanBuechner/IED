#include "ProjectIncludeTest.h"
#include "Compiler/ProjectFile.h"
#include "Compiler/SourceMacros.h"
#include "SourceManager/SourceManager.h"
#include <fstream>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// A workspace in the temp folder with Game -> Engine -> Core, each project's sources in <name>/src.
		struct Workspace
		{
			fs::path root;
			SourceManager sources;

			explicit Workspace(const char* name) : root(fs::temp_directory_path() / (std::string("c4_projects_") + name))
			{
				std::error_code ec;
				fs::remove_all(root, ec);
				fs::create_directories(root, ec);
			}
			~Workspace() { std::error_code ec; fs::remove_all(root, ec); }

			void Write(const std::string& file, const std::string& text)
			{
				const fs::path path = root / file;
				fs::create_directories(path.parent_path());
				std::ofstream(path, std::ios::binary) << text;
			}

			// Checks `description` (folders relative to the workspace), then registers every project's sources.
			void Register(const std::string& description)
			{
				ProjectSetResult checked = ProjectSet::Check(ParseProjectFile(description, root).list);
				sources.SetProjects(std::move(*checked.set));
				for (uint32 p = 0; p < sources.Projects().Count(); ++p)
					for (const fs::path& folder : sources.Projects().At(p).sources) sources.RegisterFolder(folder);
			}

			uint32 Id(const std::string& file) const
			{
				for (uint32 id = 1; id <= sources.Count(); ++id)
					if (sources.HasInfo(id) && fs::path(sources.GetInfo(id).absPath) == (root / file).lexically_normal()) return id;
				return 0;
			}

			std::string Text(const std::vector<MacroPass::FileResult>& results, const std::string& file) const
			{
				for (const auto& r : results)
					if (fs::path(r.absPath) == (root / file).lexically_normal()) return r.text;
				return "";
			}
		};

		const char* const kChain =
			"root = \"Game\"\n"
			"[[project]]\nname = \"Game\"\nsources = [\"Game/src\"]\ndepends = [\"Engine\"]\n[project.defines]\nMODE = \"game\"\n"
			"[[project]]\nname = \"Engine\"\nsources = [\"Engine/src\"]\ndepends = [\"Core\"]\n[project.defines]\nMODE = \"engine\"\nFAST = \"1\"\n"
			"[[project]]\nname = \"Core\"\nsources = [\"Core/src\"]\n";
	}

	void ProjectIncludeTest::FilesKnowTheirProject()
	{
		Workspace w("owner");
		w.Write("Game/src/main.cf", "");
		w.Write("Engine/src/render/mesh.ch", "");
		w.Write("Core/src/core.ch", "");
		w.Register(kChain);
		TEST_ASSERT_EQ(w.sources.GetInfo(w.Id("Game/src/main.cf")).project, (uint32)0);
		TEST_ASSERT_EQ(w.sources.GetInfo(w.Id("Engine/src/render/mesh.ch")).project, (uint32)1);
		TEST_ASSERT_EQ(w.sources.GetInfo(w.Id("Core/src/core.ch")).project, (uint32)2);
		TEST_PASSED();
	}

	void ProjectIncludeTest::BesideTheIncluderComesFirst()
	{
		Workspace w("beside");
		w.Write("Game/src/main.cf", "");
		w.Write("Game/src/ui/menu.cf", "");
		w.Write("Game/src/ui/util.ch", "");
		w.Write("Game/src/util.ch", "");
		w.Write("Engine/src/e.ch", "");
		w.Write("Core/src/c.ch", "");
		w.Register(kChain);
		TEST_ASSERT_EQ(w.sources.ResolveInclude("util.ch", w.Id("Game/src/ui/menu.cf")).id, w.Id("Game/src/ui/util.ch"));
		TEST_ASSERT_EQ(w.sources.ResolveInclude("util.ch", w.Id("Game/src/main.cf")).id, w.Id("Game/src/util.ch"));
		TEST_ASSERT_EQ(w.sources.ResolveInclude("../util.ch", w.Id("Game/src/ui/menu.cf")).id, w.Id("Game/src/util.ch"));
		TEST_PASSED();
	}

	void ProjectIncludeTest::OwnProjectBeforeDependencies()
	{
		Workspace w("own");
		w.Write("Game/src/main.cf", "");
		w.Write("Game/src/math.ch", "");
		w.Write("Engine/src/math.ch", "");
		w.Write("Engine/src/render.ch", "");
		w.Write("Core/src/c.ch", "");
		w.Register(kChain);
		TEST_ASSERT_EQ(w.sources.ResolveInclude("math.ch", w.Id("Game/src/main.cf")).id, w.Id("Game/src/math.ch"));
		TEST_ASSERT_EQ(w.sources.ResolveInclude("render.ch", w.Id("Game/src/main.cf")).id, w.Id("Engine/src/render.ch"));
		TEST_PASSED();
	}

	void ProjectIncludeTest::ADependencyOfADependencyIsReached()
	{
		Workspace w("deep");
		w.Write("Game/src/main.cf", "");
		w.Write("Engine/src/e.ch", "");
		w.Write("Core/src/core.ch", "");
		w.Register(kChain);
		TEST_ASSERT_EQ(w.sources.ResolveInclude("core.ch", w.Id("Game/src/main.cf")).id, w.Id("Core/src/core.ch"));
		TEST_PASSED();
	}

	void ProjectIncludeTest::TwoDependenciesWithOnePathAreAmbiguous()
	{
		Workspace w("ambiguous");
		w.Write("Game/src/main.cf", "");
		w.Write("Engine/src/util.ch", "");
		w.Write("Engine/src/engine.cf", "");
		w.Write("Core/src/util.ch", "");
		w.Register(kChain);
		const IncludeTarget target = w.sources.ResolveInclude("util.ch", w.Id("Game/src/main.cf"));
		TEST_ASSERT_EQ(target.id, (uint32)0);
		TEST_ASSERT(target.error.find("'util.ch' is a file of both 'Engine'") == 0);
		TEST_ASSERT(target.error.find("and 'Core'") != std::string::npos);
		// The engine reaches its own util.ch first: no ambiguity there.
		TEST_ASSERT_EQ(w.sources.ResolveInclude("util.ch", w.Id("Engine/src/engine.cf")).id, w.Id("Engine/src/util.ch"));
		TEST_PASSED();
	}

	void ProjectIncludeTest::TwoSourceFoldersWithOnePathAreAmbiguous()
	{
		// IED's server: src/ and every built-in plugin's code/ are source folders of one project (BUG-400).
		Workspace w("folders");
		w.Write("Game/git/Service.ch", "");
		w.Write("Game/chat/Service.ch", "");
		w.Write("Game/chat/only.ch", "");
		w.Write("Game/chat/aitools/tools.cf", "");
		w.Register("root = \"Game\"\n[[project]]\nname = \"Game\"\nsources = [\"Game/git\", \"Game/chat\"]\n");
		const IncludeTarget target = w.sources.ResolveInclude("Service.ch", w.Id("Game/chat/aitools/tools.cf"));
		TEST_ASSERT_EQ(target.id, (uint32)0);
		TEST_ASSERT(target.error.find("'Service.ch' is in two source folders of project 'Game'") == 0);
		// A path one folder has, and a file beside its includer, are no ambiguity.
		TEST_ASSERT_EQ(w.sources.ResolveInclude("only.ch", w.Id("Game/chat/aitools/tools.cf")).id, w.Id("Game/chat/only.ch"));
		TEST_ASSERT_EQ(w.sources.ResolveInclude("Service.ch", w.Id("Game/chat/only.ch")).id, w.Id("Game/chat/Service.ch"));
		TEST_PASSED();
	}

	void ProjectIncludeTest::AProjectCannotReachItsUser()
	{
		Workspace w("reach");
		w.Write("Game/src/player.ch", "");
		w.Write("Engine/src/engine.cf", "");
		w.Write("Core/src/core.cf", "");
		w.Register(kChain);
		const IncludeTarget fromEngine = w.sources.ResolveInclude("player.ch", w.Id("Engine/src/engine.cf"));
		TEST_ASSERT_EQ(fromEngine.id, (uint32)0);
		TEST_ASSERT_EQ(fromEngine.error, std::string("cannot include 'player.ch': it is a file of project 'Game', and project 'Engine' does not depend on 'Game'"));
		TEST_ASSERT(w.sources.ResolveInclude("engine.cf", w.Id("Core/src/core.cf")).error.find("project 'Core' does not depend on 'Engine'") != std::string::npos);
		TEST_ASSERT_EQ(w.sources.ResolveInclude("nothing.ch", w.Id("Core/src/core.cf")).error, std::string());
		TEST_PASSED();
	}

	void ProjectIncludeTest::EachFileStartsWithItsProjectsMacros()
	{
		Workspace w("defines");
		w.Write("Game/src/main.cf", "");
		w.Write("Engine/src/engine.cf", "#ifdef FAST\nint32 fast = 1;\n#endif\n#ifdef MODE\nconst char* m = MODE;\n#endif\n");
		w.Write("Core/src/core.cf", "#ifdef MODE\nint32 leaked = 1;\n#endif\n");
		w.Register(kChain);
		const auto results = ExpandSourceMacros(w.sources, { { "SHARED", "1" } });
		TEST_ASSERT(w.Text(results, "Engine/src/engine.cf").find("int32 fast = 1;") != std::string::npos);
		TEST_ASSERT(w.Text(results, "Engine/src/engine.cf").find("const char* m = engine;") != std::string::npos);
		TEST_ASSERT(w.Text(results, "Core/src/core.cf").find("leaked") == std::string::npos);
		TEST_PASSED();
	}

	void ProjectIncludeTest::AnIncludeCarriesOnlyWhatTheFileDefines()
	{
		Workspace w("export");
		w.Write("Engine/src/engine.ch", "#define ENGINE_API 7\n");
		w.Write("Game/src/main.cf", "include \"engine.ch\"\nint32 a = ENGINE_API;\n#ifdef FAST\nint32 leaked = 1;\n#endif\nconst char* m = MODE;\n");
		w.Write("Core/src/c.ch", "");
		w.Register(kChain);
		const std::string game = w.Text(ExpandSourceMacros(w.sources, {}), "Game/src/main.cf");
		TEST_ASSERT(game.find("int32 a = 7;") != std::string::npos);  // the header's own #define reaches the includer
		TEST_ASSERT(game.find("leaked") == std::string::npos);        // the engine's project define does not
		TEST_ASSERT(game.find("const char* m = game;") != std::string::npos); // nor does its MODE replace the game's
		TEST_PASSED();
	}
} // namespace Testing
