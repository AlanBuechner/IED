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
		// For Linux `-l<path>` (a C++ dependency's .a) is the file; `-l<name>` stays a name ld searches for.
		TEST_FUNC(Compiler.Link)
		void LinuxLibraryFilesGoAsFiles();
	};
} // namespace Testing
