#include "VirtualSourceTest.h"
#include "Compiler/Projects.h"
#include "SourceManager/SourceManager.h"

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		// One project, Engine, holding the files of its folder `//vt/Engine` - a path Linux spells `/vt/Engine`.
		void OneProject(SourceManager& sources)
		{
			ProjectList list;
			list.projects.push_back({ "Engine", {}, {}, {}, {} });
			list.root = "Engine";
			ProjectSetResult checked = ProjectSet::Check(std::move(list));
			sources.SetProjects(std::move(*checked.set));
		}
	}

	void VirtualSourceTest::AVirtualSourceIsFoundByItsNormalPath()
	{
		SourceManager sources;
		const uint32 dotted = sources.RegisterVirtual("/vt/./Engine/a.ch", "a.ch", "int32 A();\n", 0, false);
		const uint32 doubled = sources.RegisterVirtual("//vt/Engine/b.ch", "b.ch", "int32 B();\n", 0, false);
		TEST_ASSERT(dotted != 0 && doubled != 0);
		TEST_ASSERT_EQ(sources.FindByAbsPath("/vt/Engine/a.ch"), dotted);
		TEST_ASSERT_EQ(sources.FindByAbsPath("//vt/Engine/b.ch"), doubled);
		TEST_ASSERT_EQ(sources.RegisterVirtual("/vt/Engine/a.ch", "a.ch", "", 0, false), 0u); // the same file, by another spelling
		TEST_PASSED();
	}

	void VirtualSourceTest::AnIncludeBesideAVirtualSourceIsFound()
	{
		SourceManager sources;
		OneProject(sources);
		const uint32 header = sources.RegisterVirtual("//vt/Engine/engine.ch", "engine.ch", "int32 Area();\n", 0, false);
		const uint32 user = sources.RegisterVirtual("//vt/Engine/main.cf", "main.cf", "include \"engine.ch\"\n", 0, false);
		TEST_ASSERT_EQ(sources.ResolveIncludePath("engine.ch", user), header);
		TEST_PASSED();
	}

	void VirtualSourceTest::AVirtualFolderHoldsItsProjectsFiles()
	{
		SourceManager sources;
		OneProject(sources);
		sources.AddFolder("//vt/Engine", 0);
		const uint32 header = sources.RegisterVirtual("//vt/Engine/engine.ch", "engine.ch", "int32 Area();\n", 0, false);
		const uint32 user = sources.RegisterVirtual("//vt/Engine/sub/main.cf", "sub/main.cf", "include \"engine.ch\"\n", 0, false);
		TEST_ASSERT_EQ(sources.ResolveIncludePath("engine.ch", user), header); // not beside it: in its project's folder
		TEST_PASSED();
	}
} // namespace Testing
