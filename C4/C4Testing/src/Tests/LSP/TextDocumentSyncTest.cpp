#include "TextDocumentSyncTest.h"
#include <vector>

using namespace C4LSP::Protocol;

namespace Testing
{
	TextContentChangeEvent TextDocumentSyncTest::MakeChange(uint32 startLine, uint32 startChar,
		uint32 endLine, uint32 endChar, const std::string& text)
	{
		TextContentChangeEvent change;
		Range range;
		range.start.line = startLine;
		range.start.character = startChar;
		range.end.line = endLine;
		range.end.character = endChar;
		change.range = range;
		change.text = text;
		return change;
	}

	// BUG-447: a change without a range is the whole document, also among ranged ones, which apply to what it left.
	void TextDocumentSyncTest::TestAChangeWithoutARangeReplacesTheDocument()
	{
		TextDocumentSync sync;
		sync.SetFullText("int x = 5;");
		TextContentChangeEvent whole;
		whole.text = "int y = 6;\nint z;";
		sync.ApplyChanges({ whole, MakeChange(1, 4, 1, 5, "w") });
		TEST_ASSERT_EQ(sync.GetText(), std::string("int y = 6;\nint w;"));
	}

	// A UTF-16 client counts a character outside ASCII as one unit (two past U+FFFF); a UTF-8 one counts bytes.
	void TextDocumentSyncTest::TestColumnsCountTheAgreedEncoding()
	{
		const std::string before = "// \xC3\xA9t\xC3\xA9 \xF0\x9F\x98\x80 x\nint a;"; // "// ete <emoji> x", accented
		TextDocumentSync sync;
		sync.SetFullText(before);
		sync.ApplyChanges({ MakeChange(0, 10, 0, 11, "y") }, PositionEncoding::Utf16); // x after 3 + 3 + 1 + 2 + 1 units
		TEST_ASSERT_EQ(sync.GetText(), std::string("// \xC3\xA9t\xC3\xA9 \xF0\x9F\x98\x80 y\nint a;"));
		sync.ApplyChanges({ MakeChange(0, 14, 0, 15, "z") }, PositionEncoding::Utf8); // y after 14 bytes
		TEST_ASSERT_EQ(sync.GetText(), std::string("// \xC3\xA9t\xC3\xA9 \xF0\x9F\x98\x80 z\nint a;"));
	}

	void TextDocumentSyncTest::TestIncrementalInsertAtStart()
	{
		TextDocumentSync sync;
		sync.SetFullText("int x = 5;\nint y = 6;");

		std::vector<TextContentChangeEvent> changes = { MakeChange(0, 0, 0, 0, "hello ") };
		sync.ApplyChanges(changes);

		TEST_ASSERT_EQ(sync.GetText(), std::string("hello int x = 5;\nint y = 6;"));
		TEST_PASSED();
	}

	void TextDocumentSyncTest::TestIncrementalDeleteRange()
	{
		TextDocumentSync sync;
		sync.SetFullText("int x = 5;\nint y = 6;");

		// Delete [4,9) on the first line: removes "x = 5" (cols 4..8), leaving "int ;".
		std::vector<TextContentChangeEvent> changes = { MakeChange(0, 4, 0, 9, "") };
		sync.ApplyChanges(changes);

		TEST_ASSERT_EQ(sync.GetText(), std::string("int ;\nint y = 6;"));
		TEST_PASSED();
	}

	void TextDocumentSyncTest::TestIncrementalReplaceAcrossLines()
	{
		TextDocumentSync sync;
		sync.SetFullText("alpha\nbeta\ngamma");

		// Replace from the start of line 1 through the end of "beta" with a single token.
		std::vector<TextContentChangeEvent> changes = { MakeChange(0, 0, 1, 4, "middle") };
		sync.ApplyChanges(changes);

		TEST_ASSERT_EQ(sync.GetText(), std::string("middle\ngamma"));
		TEST_PASSED();
	}

	void TextDocumentSyncTest::TestMultipleSequentialChanges()
	{
		TextDocumentSync sync;
		sync.SetFullText("abcdef");

		std::vector<TextContentChangeEvent> changes = {
			MakeChange(0, 1, 0, 3, ""),   // delete [1,3) -> removes "bc" -> "adef"
			MakeChange(0, 0, 0, 0, "XYZ"), // insert at start -> "XYZadef"
		};
		sync.ApplyChanges(changes);

		TEST_ASSERT_EQ(sync.GetText(), std::string("XYZadef"));
		TEST_PASSED();
	}

	void TextDocumentSyncTest::TestReplaceWholeBufferViaFullRange()
	{
		TextDocumentSync sync;
		sync.SetFullText("int x = 5;\nint y = 6;");

		// A change spanning the entire document (0,0 to end of line 1) replaces everything.
		std::vector<TextContentChangeEvent> changes = { MakeChange(0, 0, 1, 10, "replaced") };
		sync.ApplyChanges(changes);

		TEST_ASSERT_EQ(sync.GetText(), std::string("replaced"));
		TEST_PASSED();
	}
} // namespace Testing
