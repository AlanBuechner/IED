#include "TextDocumentStoreTest.h"
#include <vector>
#include <string>

using namespace C4LSP::Protocol;

namespace Testing
{
	void TextDocumentStoreTest::TestOpenTracksTextAndVersion()
	{
		TextDocumentStore store;
		store.Open("file:///a.cf", "int a;", 3);

		TEST_ASSERT(store.IsOpen("file:///a.cf"));
		TEST_ASSERT_EQ(*store.GetText("file:///a.cf"), std::string("int a;"));
		TEST_ASSERT_EQ(store.GetVersion("file:///a.cf"), (int)3);
		TEST_PASSED();
	}

	void TextDocumentStoreTest::TestFullChangeReplacesWholeText()
	{
		TextDocumentStore store;
		store.Open("file:///a.cf", "int a;", 1);

		// Full sync: a single change with no range carries the entire new text.
		std::vector<TextContentChangeEvent> changes(1);
		changes[0].text = "int b = 2;";
		store.Change("file:///a.cf", changes, /*fullSync=*/true);

		TEST_ASSERT_EQ(*store.GetText("file:///a.cf"), std::string("int b = 2;"));
		TEST_ASSERT_EQ(store.GetVersion("file:///a.cf"), (int)2);
		TEST_PASSED();
	}

	void TextDocumentStoreTest::TestIncrementalChangeSplicesBuffer()
	{
		TextDocumentStore store;
		store.Open("file:///a.cf", "int x;", 1);

		// Insert "y = " after "int " (column 4).
		std::vector<TextContentChangeEvent> changes(1);
		changes[0].range.start.line = 0;
		changes[0].range.start.character = 4;
		changes[0].range.end.line = 0;
		changes[0].range.end.character = 4;
		changes[0].text = "y = ";
		store.Change("file:///a.cf", changes, /*fullSync=*/false);

		TEST_ASSERT_EQ(*store.GetText("file:///a.cf"), std::string("int y = x;"));
		TEST_PASSED();
	}

	void TextDocumentStoreTest::TestCloseRemovesDocument()
	{
		TextDocumentStore store;
		store.Open("file:///a.cf", "int a;", 1);
		TEST_ASSERT(store.IsOpen("file:///a.cf"));

		store.Close("file:///a.cf");
		TEST_ASSERT(!store.IsOpen("file:///a.cf"));
		TEST_ASSERT_NULL(store.GetText("file:///a.cf"));
		TEST_PASSED();
	}

	void TextDocumentStoreTest::TestForEachVisitsAllOpenDocuments()
	{
		TextDocumentStore store;
		store.Open("file:///a.cf", "A", 1);
		store.Open("file:///b.cf", "B", 1);

		int visited = 0;
		std::string joined;
		store.ForEachDocument([&](const std::string&, const std::string& text) {
			++visited;
			joined += text;
		});

		TEST_ASSERT_EQ(visited, (int)2);
		TEST_ASSERT(joined.find("A") != std::string::npos);
		TEST_ASSERT(joined.find("B") != std::string::npos);
		TEST_PASSED();
	}
} // namespace Testing
