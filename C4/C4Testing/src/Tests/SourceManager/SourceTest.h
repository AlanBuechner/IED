#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "SourceManager/Source.h"
#include "SourceManager/SourceManager.h"

namespace Testing
{
	CLASS(GROUP = Testing) SourceTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::SourceTest);

		TEST_FUNC(Source.Memory)
		void TestMemoryEmptySource();

		TEST_FUNC(Source.Memory)
		void TestMemorySequentialReadToEOF();

		TEST_FUNC(Source.Memory)
		void TestMemoryPushBack();

		TEST_FUNC(Source.Memory)
		void TestMemoryPushBackAtStartClamps();

		TEST_FUNC(Source.Memory)
		void TestMemorySeekThenRead();

		TEST_FUNC(Source.Memory)
		void TestMemorySeekBeyondEndClamps();

		TEST_FUNC(Source.Memory)
		void TestMemoryMultipleSeeks();

		TEST_FUNC(Source.Memory)
		void TestMemoryReadCharAtOutOfRange();

		TEST_FUNC(Source.Memory)
		void TestMemoryHighByteValues();

		// A leading UTF-8 BOM (EF BB BF) must be stripped so it doesn't glue to the first token.
		TEST_FUNC(Source.Memory)
		void TestMemoryStripsUtf8Bom();

		// A file saved with a BOM (the Visual Studio / Notepad default) must read back without one.
		TEST_FUNC(Source.File)
		void TestFileStripsUtf8Bom();

		TEST_FUNC(Source.Lines)
		void TestLineToByteOffsetMultiLine();

		TEST_FUNC(Source.Lines)
		void TestLineToByteOffsetOutOfRange();

		TEST_FUNC(Source.Lines)
		void TestGetLineTextExcludesNewline();

		TEST_FUNC(Source.Lines)
		void TestGetLineTextLastLineNoNewline();

		TEST_FUNC(Source.Lines)
		void TestGetLineTextInvalidLine();

		TEST_FUNC(Source.Lines)
		void TestByteOffsetToLineColumn();

		TEST_FUNC(Source.Lines)
		void TestByteOffsetToLineColumnEmptySource();

		TEST_FUNC(Source.File)
		void TestFileReadsDiskContent();

		TEST_FUNC(Source.File)
		void TestFileMissingPathIsEmpty();

		TEST_FUNC(SourceManager)
		void TestRegisterInMemory();

		TEST_FUNC(SourceManager)
		void TestRegisterInMemoryDuplicateName();

		TEST_FUNC(SourceManager)
		void TestRegisterFileNotFound();

		TEST_FUNC(SourceManager)
		void TestSetSourceTextRebuildsLineTable();

		TEST_FUNC(SourceManager)
		void TestGetLineTextThroughManager();

		// ResolveIncludePath - exact, substring-safety, requester-relative, and no basename fallback.
		TEST_FUNC(SourceManager.Resolve)
		void TestResolveIncludeExactName();

		// "math.ch" must NOT resolve to a registered "cmath.ch" (substring leakage).
		TEST_FUNC(SourceManager.Resolve)
		void TestResolveIncludeSubstringDoesNotLeak();

		// An include relative to the requesting source's directory.
		TEST_FUNC(SourceManager.Resolve)
		void TestResolveIncludeRelativeFromRequester();

		// A bare include name does not reach a file registered under a nested relative path.
		TEST_FUNC(SourceManager.Resolve)
		void TestResolveIncludeHasNoBasenameFallback();
	};
} // namespace Testing
