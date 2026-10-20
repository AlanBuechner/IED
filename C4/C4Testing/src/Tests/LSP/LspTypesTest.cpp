#include "LspTypesTest.h"

using namespace C4LSP::Protocol;

namespace Testing
{
	void LspTypesTest::TestPositionConvertsOneBasedToZeroBased()
	{
		Scripting::SourceLocation loc;
		loc.line = 5;     // 1-based
		loc.column = 12;  // 1-based
		loc.byteOffset = 78;

		Position p = Position::FromSourceLocation(loc);
		TEST_ASSERT_EQ(p.line, (uint32)4);      // 0-based
		TEST_ASSERT_EQ(p.character, (uint32)11); // 0-based
		TEST_PASSED();
	}

	void LspTypesTest::TestRangeFromSourceRange()
	{
		Scripting::SourceLocation start;
		start.line = 1;
		start.column = 1;
		Scripting::SourceLocation end;
		end.line = 3;
		end.column = 9;

		Range r = Range::FromSourceRange(start, end);
		TEST_ASSERT_EQ(r.start.line, (uint32)0);
		TEST_ASSERT_EQ(r.start.character, (uint32)0);
		TEST_ASSERT_EQ(r.end.line, (uint32)2);
		TEST_ASSERT_EQ(r.end.character, (uint32)8);
		TEST_PASSED();
	}

	void LspTypesTest::TestUriToUriForWindowsPath()
	{
		std::string uri = DocumentUri::ToUri("C:\\Work\\Game\\Main.cf");
		TEST_ASSERT_EQ(uri, std::string("file:///C:/Work/Game/Main.cf"));
		TEST_PASSED();
	}

	void LspTypesTest::TestUriRoundTripNormalizesSeparators()
	{
		const std::string original = "C:\\Work\\Game\\src\\Main.cf";
		std::string uri = DocumentUri::ToUri(original);
		std::string back = DocumentUri::FromUri(uri);
		TEST_ASSERT_EQ(back, original);
		TEST_PASSED();
	}

	void LspTypesTest::TestLocationMakeUsesConvertedRange()
	{
		Scripting::SourceLocation start;
		start.line = 2;
		start.column = 4;
		Scripting::SourceLocation end;
		end.line = 2;
		end.column = 7;

		Location loc = Location::Make("file:///x.cf", start, end);
		TEST_ASSERT_EQ(loc.uri, std::string("file:///x.cf"));
		TEST_ASSERT_EQ(loc.range.start.line, (uint32)1);
		TEST_ASSERT_EQ(loc.range.start.character, (uint32)3);
		TEST_ASSERT_EQ(loc.range.end.character, (uint32)6);
		TEST_PASSED();
	}
} // namespace Testing
