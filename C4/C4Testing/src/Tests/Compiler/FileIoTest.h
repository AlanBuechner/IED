#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Files by path through LLVM (Compiler/FileIo.h, BUG-354): what the compiler's caches read and write, in folders
	// whose paths are longer than MAX_PATH too.
	CLASS(GROUP = Testing) FileIoTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::FileIoTest);

		TEST_FUNC(Compiler.FileIo)
		void ADeepFileReadsBackWhatWasWritten();
		TEST_FUNC(Compiler.FileIo)
		void AMissingFileHasNoBytesAndNoStamp();
		TEST_FUNC(Compiler.FileIo)
		void AFileInAMissingFolderIsNotWritten();
	};
} // namespace Testing
