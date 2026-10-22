#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What a link hands lld-link (Compiler/BuildUtils: LinkerArguments): clang's driver's translation of `-l`, `-L` and
	// `-Wl,` arguments, which the compiler now does itself.
	CLASS(GROUP = Testing) LinkArgumentsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LinkArgumentsTest);

		TEST_FUNC(Compiler.Link)
		void LibrariesAreNamedWithTheirExtension();
		TEST_FUNC(Compiler.Link)
		void SearchFoldersAndLinkerArgumentsAreLld();
		TEST_FUNC(Compiler.Link)
		void FilesGoAsTheyAre();
	};
} // namespace Testing
