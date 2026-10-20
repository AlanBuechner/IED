#include "TextDocumentSyncTest.h"
#include <vector>

using namespace C4LSP::Protocol;

namespace Testing
{
	TextContentChangeEvent TextDocumentSyncTest::MakeChange(uint32 startLine, uint32 startChar,
		uint32 endLine, uint32 endChar, const std::string& text)
	{
		TextContentChangeEvent change;
		change.range.start.line = startLine;
		change.range.start.character = startChar;
		change.range.end.line = endLine;
		change.range.end.character = endChar;
		change.text = text;
		return change;
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
