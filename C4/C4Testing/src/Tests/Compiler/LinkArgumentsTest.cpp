#include "LinkArgumentsTest.h"
#include "Compiler/BuildUtils.h"
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;
	using Arguments = std::vector<std::string>;

	void LinkArgumentsTest::LibrariesAreNamedWithTheirExtension()
	{
		TEST_ASSERT(LinkerArguments("-lws2_32") == Arguments{ "ws2_32.lib" });
		TEST_ASSERT(LinkerArguments("-ldelayimp.lib") == Arguments{ "delayimp.lib" });
		TEST_ASSERT(LinkerArguments("-lUser32.LIB") == Arguments{ "User32.LIB" });
		TEST_PASSED();
	}

	void LinkArgumentsTest::SearchFoldersAndLinkerArgumentsAreLld()
	{
		TEST_ASSERT(LinkerArguments("-LC:/a b/libs") == Arguments{ "-libpath:C:/a b/libs" });
		TEST_ASSERT(LinkerArguments("-Wl,/DEBUG") == Arguments{ "/DEBUG" });
		TEST_ASSERT((LinkerArguments("-Wl,/DEBUG,/OPT:REF") == Arguments{ "/DEBUG", "/OPT:REF" }));
		TEST_ASSERT(LinkerArguments("-Wl,/PDB:C:/out dir/app.pdb") == Arguments{ "/PDB:C:/out dir/app.pdb" });
		TEST_ASSERT(LinkerArguments("-Wl,") == Arguments{});
		TEST_PASSED();
	}

	void LinkArgumentsTest::FilesGoAsTheyAre()
	{
		TEST_ASSERT(LinkerArguments("C:/libs/foreign.lib") == Arguments{ "C:/libs/foreign.lib" });
		TEST_ASSERT(LinkerArguments("app.res") == Arguments{ "app.res" });
		TEST_ASSERT(LinkerArguments("C:/int/app.objects/a-b.obj") == Arguments{ "C:/int/app.objects/a-b.obj" });
		TEST_PASSED();
	}
} // namespace Testing
