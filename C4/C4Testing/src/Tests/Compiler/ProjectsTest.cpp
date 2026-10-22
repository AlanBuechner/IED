#include "ProjectsTest.h"
#include "Compiler/FileIo.h"
#include "Compiler/ProjectFile.h"
#include <process.h>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const fs::path kFolder = fs::path("C:/Work/build");

		ProjectList Parsed(const char* text)
		{
			ProjectFileResult read = ParseProjectFile(text, kFolder);
			return read.list;
		}

		std::string ParseError(const char* text) { return ParseProjectFile(text, kFolder).error; }

		std::string CheckError(const char* text) { return ProjectSet::Check(Parsed(text)).error; }

		ProjectSet Checked(const char* text) { return *ProjectSet::Check(Parsed(text)).set; }

		const char* const kGameEngineCore =
			"root = \"Game\"\n"
			"[[project]]\nname = \"Game\"\nsources = [\"C:/Work/Game/src\"]\ndepends = [\"Engine\"]\n"
			"[[project]]\nname = \"Engine\"\nsources = [\"C:/Work/Engine/src\"]\ndepends = [\"Core\"]\n"
			"[[project]]\nname = \"Core\"\nsources = [\"C:/Work/Core/src\"]\ndepends = []\n";
	}

	void ProjectsTest::ReadsADescription()
	{
		ProjectFileResult read = ParseProjectFile(
			"# written by nuke\n"
			"root = \"Game\"\n"
			"\n"
			"[[project]]\n"
			"name = \"Game\"\n"
			"sources = [\"../Game/src\"]   # relative to the description\n"
			"depends = [\n"
			"    \"Engine\",\n"
			"]\n"
			"[project.defines]\n"
			"PROJECT_NAME = \"Game\"\n"
			"QUOTE = \"say \\\"hi\\\"\"\n"
			"\n"
			"[[project]]\n"
			"name = \"Engine\"\n"
			"sources = [\"C:/Work/Engine/src\", \"C:/Work/Engine/gen\"]\n"
			"includes = [\"C:/Work/Engine/third_party\"]\n", kFolder);
		TEST_ASSERT_EQ(read.error, std::string());
		TEST_ASSERT_EQ(read.list.root, std::string("Game"));
		TEST_ASSERT_EQ((int)read.list.projects.size(), 2);
		const Project& game = read.list.projects[0];
		TEST_ASSERT_EQ(game.sources[0].generic_string(), std::string("C:/Work/Game/src"));
		TEST_ASSERT_EQ((int)game.depends.size(), 1);
		TEST_ASSERT_EQ(game.depends[0], std::string("Engine"));
		TEST_ASSERT_EQ(game.defines.at("PROJECT_NAME"), std::string("Game"));
		TEST_ASSERT_EQ(game.defines.at("QUOTE"), std::string("say \"hi\""));
		const Project& engine = read.list.projects[1];
		TEST_ASSERT_EQ((int)engine.sources.size(), 2);
		TEST_ASSERT_EQ(engine.includes[0].generic_string(), std::string("C:/Work/Engine/third_party"));
		TEST_ASSERT(engine.depends.empty() && engine.defines.empty());
		TEST_PASSED();
	}

	void ProjectsTest::AnErrorNamesItsLine()
	{
		TEST_ASSERT_EQ(ParseError("root = \"A\"\n[[project]]\nname = \"A\"\nsoruces = [\"x\"]\n"),
			std::string("line 4: 'soruces' is not a key of a project (name, sources, includes, depends, [project.defines])"));
		TEST_ASSERT_EQ(ParseError("root = \"A\nname = \"B\"\n"), std::string("line 1: a string ends at the end of its line"));
		TEST_ASSERT_EQ(ParseError("[tools]\n"), std::string("line 1: '[tools]' is not a table of a build description"));
		TEST_ASSERT_EQ(ParseError("[project.defines]\n"), std::string("line 1: [project.defines] comes after the [[project]] it belongs to"));
		TEST_ASSERT_EQ(ParseError("[[project]]\nname = \"A\"\nname = \"B\"\n"), std::string("line 3: 'name' is set twice in one project"));
		TEST_ASSERT_EQ(ParseError("[[project]]\nsources = \"src\"\n"), std::string("line 2: sources is a [\"list\"]"));
		TEST_ASSERT_EQ(ParseError("[[project]]\n[project.defines]\nA = [\"1\"]\n"), std::string("line 3: a define's value is one \"string\""));
		TEST_ASSERT_EQ(ParseError("root = \"A\" \"B\"\n"), std::string("line 1: a line holds one table header or one 'key = value'"));
		TEST_ASSERT_EQ(ParseError("root = 3\n"), std::string("line 1: a value is a \"string\" or a [\"list\", \"of strings\"]"));
		TEST_ASSERT_EQ(ParseError("root = \"A\"\n@\n"), std::string("line 2: unexpected '@'"));
		TEST_ASSERT_EQ(ParseError("[[project]]\ndepends = [\"A\" \"B\"]\n"), std::string("line 2: a list holds \"strings\" separated by commas"));
		TEST_ASSERT_EQ(ParseError("root = \"\\q\"\n"), std::string("line 1: '\\q' is not an escape this file takes (\\\\, \\\", \\n, \\t)"));
		TEST_PASSED();
	}

	void ProjectsTest::NamesAndDependenciesAreChecked()
	{
		TEST_ASSERT_EQ(CheckError("[[project]]\nname = \"A\"\n"), std::string("the build names no root project"));
		TEST_ASSERT_EQ(CheckError("root = \"B\"\n[[project]]\nname = \"A\"\n"), std::string("the root project 'B' is not described"));
		TEST_ASSERT_EQ(CheckError("root = \"A\"\n[[project]]\nname = \"A\"\n[[project]]\nname = \"A\"\n"), std::string("two projects are named 'A'"));
		TEST_ASSERT_EQ(CheckError("root = \"A\"\n[[project]]\nname = \"A\"\ndepends = [\"B\"]\n"),
			std::string("project 'A' depends on 'B', which the build does not describe"));
		TEST_ASSERT_EQ(CheckError("root = \"A\"\n[[project]]\nname = \"A\"\ndepends = [\"A\"]\n"), std::string("project 'A' depends on itself"));
		TEST_ASSERT_EQ(CheckError(kGameEngineCore), std::string());
		TEST_PASSED();
	}

	void ProjectsTest::ACycleIsPrinted()
	{
		TEST_ASSERT_EQ(CheckError(
			"root = \"Game\"\n"
			"[[project]]\nname = \"Game\"\ndepends = [\"Engine\"]\n"
			"[[project]]\nname = \"Engine\"\ndepends = [\"Core\"]\n"
			"[[project]]\nname = \"Core\"\ndepends = [\"Engine\"]\n"),
			std::string("the projects depend on each other in a cycle: Engine -> Core -> Engine"));
		TEST_PASSED();
	}

	void ProjectsTest::AFolderBelongsToOneProject()
	{
		TEST_ASSERT_EQ(CheckError(
			"root = \"Game\"\n"
			"[[project]]\nname = \"Game\"\nsources = [\"C:/Work/Game\"]\ndepends = [\"Engine\"]\n"
			"[[project]]\nname = \"Engine\"\nsources = [\"C:/Work/Game/engine\"]\n"),
			std::string("'C:/Work/Game/engine' is in the sources of both 'Game' and 'Engine'"));
		TEST_ASSERT_EQ(CheckError(
			"root = \"Game\"\n"
			"[[project]]\nname = \"Game\"\nsources = [\"C:/Work/Game/src\"]\ndepends = [\"Engine\"]\n"
			"[[project]]\nname = \"Engine\"\nsources = [\"C:/Work/Game/src2\"]\n"), std::string());
		TEST_PASSED();
	}

	void ProjectsTest::EveryProjectServesTheRoot()
	{
		TEST_ASSERT_EQ(CheckError("root = \"A\"\n[[project]]\nname = \"A\"\n[[project]]\nname = \"Tools\"\n"),
			std::string("project 'Tools' is described, but the root 'A' does not use it"));
		TEST_PASSED();
	}

	void ProjectsTest::AProjectUsesWhatItDependsOn()
	{
		ProjectSet set = Checked(kGameEngineCore);
		TEST_ASSERT_EQ(set.At(set.Root()).name, std::string("Game"));
		TEST_ASSERT(set.CanUse(0, 0) && set.CanUse(0, 1) && set.CanUse(0, 2));
		TEST_ASSERT(set.CanUse(1, 2) && !set.CanUse(1, 0));
		TEST_ASSERT(!set.CanUse(2, 1) && !set.CanUse(2, 0));
		TEST_ASSERT_EQ((int)set.Uses(0).size(), 3);
		TEST_ASSERT_EQ((int)set.Uses(0)[1], 1); // nearest first: Engine before Core
		TEST_PASSED();
	}

	void ProjectsTest::FilesBelongToTheirProject()
	{
		ProjectSet set = Checked(kGameEngineCore);
		TEST_ASSERT_EQ((int)*set.OwnerOf("C:/Work/Engine/src/render/mesh.ch"), 1);
		TEST_ASSERT_EQ((int)*set.OwnerOf("c:/work/game/src/main.cf"), 0); // Windows paths ignore case
		TEST_ASSERT(!set.OwnerOf("C:/Work/Engine/srcs/x.cf"));          // a sibling folder with a longer name is not inside
		ProjectSet one = ProjectSet::OneProject({ "C:/Work/app", "C:/Work/lib" });
		TEST_ASSERT_EQ((int)one.Count(), 1);
		TEST_ASSERT_EQ((int)*one.OwnerOf("C:/Work/lib/x.cf"), 0);
		TEST_PASSED();
	}

	void ProjectsTest::ADeepDescriptionReads()
	{
		// BUG-356: a description in a folder whose path passes MAX_PATH reads, and its folders resolve from there.
		const fs::path root = fs::temp_directory_path() / ("c4_projects_test_" + std::to_string(_getpid()));
		const fs::path folder = root / std::string(120, 'd') / std::string(120, 'e');
		std::error_code ec;
		fs::create_directories(ExtendedPath(folder), ec);
		const fs::path file = folder / "c4project.toml";
		TEST_ASSERT(PathText(file).size() > 260);
		TEST_ASSERT(WriteFileBytes(PathText(file), "root = \"App\"\n[[project]]\nname = \"App\"\nsources = [\"src\"]\n"));
		ProjectFileResult read = ReadProjectFile(file);
		TEST_ASSERT_EQ(read.error, std::string());
		TEST_ASSERT_EQ((int)read.list.projects.size(), 1);
		TEST_ASSERT(read.list.projects[0].sources[0].lexically_normal() == (folder / "src").lexically_normal());
		ProjectFileResult missing = ReadProjectFile(folder / "missing.toml");
		TEST_ASSERT(missing.error.find("cannot read the build description") == 0 && missing.error.find('\\') == std::string::npos);
		fs::remove_all(ExtendedPath(root), ec);
		TEST_PASSED();
	}
} // namespace Testing
