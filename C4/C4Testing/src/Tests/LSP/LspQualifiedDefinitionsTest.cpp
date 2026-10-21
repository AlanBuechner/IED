#include "LspQualifiedDefinitionsTest.h"
#include "LspTestSupport.h"
#include "Features/WorkspaceSymbols.h"
#include "Features/DocumentSymbols.h"
#include "Features/Hover.h"
#include "Features/Definition.h"
#include "Features/Rename.h"
#include "Features/DocumentHighlight.h"
#include "Features/References.h"

using namespace C4LSP::Features;

namespace Testing
{
	// "Next" occurs as the declaration (1), the out-of-line definition (2) and the call (3); "Counter" as the class
	// (1), the definition's qualifier (2) and the local's type (3).
	static const char* kProgram =
		R"(class Counter
{
	public int32 value;
	public int32 Next();
}

int32 Counter::Next() {
	value = value + 1;
	return value;
}

int32 main() {
	Counter c;
	return c.Next();
}
)";

	static bool At(const C4LSP::Protocol::Position& a, const C4LSP::Protocol::Position& b)
	{
		return a.line == b.line && a.character == b.character;
	}

	void LspQualifiedDefinitionsTest::TestWorkspaceSymbolsNameTheMethod()
	{
		auto session = Testing::Lsp::MakeSession("Qualified.cf", kProgram);
		auto definition = Testing::Lsp::PositionOf(kProgram, "Next", 2);
		bool found = false;
		for (const auto& symbol : WorkspaceSymbols::ForQuery(session, "next"))
			if (symbol.name == "Next" && At(symbol.location.range.start, definition))
				found = true;
		TEST_ASSERT(found);
		for (const auto& symbol : WorkspaceSymbols::ForQuery(session, "counter"))
			TEST_ASSERT(symbol.name == "Counter" && symbol.kind == 5 /* Class */);
		TEST_PASSED();
	}

	void LspQualifiedDefinitionsTest::TestDocumentSymbolsNameTheMethod()
	{
		auto session = Testing::Lsp::MakeSession("Qualified.cf", kProgram);
		auto definition = Testing::Lsp::PositionOf(kProgram, "Next", 2);
		bool found = false;
		for (const auto& symbol : DocumentSymbols::ForDocument(session, Testing::Lsp::UriFor("Qualified.cf")))
		{
			TEST_ASSERT(!(symbol.name == "Counter" && symbol.kind == 12 /* Function */));
			if (symbol.name == "Next" && At(symbol.selectionRange.start, definition))
				found = true;
		}
		TEST_ASSERT(found);
		TEST_PASSED();
	}

	void LspQualifiedDefinitionsTest::TestHoverNamesTheMethod()
	{
		auto session = Testing::Lsp::MakeSession("Qualified.cf", kProgram);
		auto result = Hover::At(session, Testing::Lsp::UriFor("Qualified.cf"), Testing::Lsp::PositionOf(kProgram, "Next", 2));
		TEST_ASSERT(result.found);
		TEST_ASSERT(Testing::Lsp::Contains(result.contents, "Next("));
		TEST_ASSERT(!Testing::Lsp::Contains(result.contents, "Counter("));
		TEST_PASSED();
	}

	void LspQualifiedDefinitionsTest::TestDefinitionLandsOnTheBody()
	{
		auto session = Testing::Lsp::MakeSession("Qualified.cf", kProgram);
		auto locations = Definition::At(session, Testing::Lsp::UriFor("Qualified.cf"), Testing::Lsp::PositionOf(kProgram, "Next", 3));
		TEST_ASSERT_EQ(locations.Count(), 1u);
		TEST_ASSERT(At(locations[0].range.start, Testing::Lsp::PositionOf(kProgram, "Next", 2)));
		TEST_PASSED();
	}

	void LspQualifiedDefinitionsTest::TestRenameLeavesTheQualifyingType()
	{
		auto session = Testing::Lsp::MakeSession("Qualified.cf", kProgram);
		auto result = Rename::At(session, Testing::Lsp::UriFor("Qualified.cf"), Testing::Lsp::PositionOf(kProgram, "Next", 3));
		TEST_ASSERT(result.found);
		for (const auto& occurrence : result.occurrences)
			TEST_ASSERT(!At(occurrence.range.start, Testing::Lsp::PositionOf(kProgram, "Counter", 2)));
		// The declaration, the out-of-line definition and the call, each once.
		TEST_ASSERT_EQ(result.occurrences.Count(), 3u);
		for (int n = 1; n <= 3; ++n)
		{
			bool found = false;
			for (const auto& occurrence : result.occurrences)
				if (At(occurrence.range.start, Testing::Lsp::PositionOf(kProgram, "Next", n)))
					found = true;
			TEST_ASSERT(found);
		}
		TEST_PASSED();
	}

	void LspQualifiedDefinitionsTest::TestHighlightMarksBothDeclarations()
	{
		auto session = Testing::Lsp::MakeSession("Qualified.cf", kProgram);
		auto highlights = DocumentHighlight::At(session, Testing::Lsp::UriFor("Qualified.cf"), Testing::Lsp::PositionOf(kProgram, "Next", 3));
		TEST_ASSERT_EQ(highlights.Count(), 3u);
		for (int n = 1; n <= 3; ++n)
		{
			bool found = false;
			for (const auto& highlight : highlights)
				if (At(highlight.range.start, Testing::Lsp::PositionOf(kProgram, "Next", n)))
					found = highlight.kind == (n < 3 ? C4LSP::Protocol::HighlightKind::Write : C4LSP::Protocol::HighlightKind::Read);
			TEST_ASSERT(found);
		}
		TEST_PASSED();
	}

	static const char* kHeader =
		R"(class Counter
{
	public int32 value;
	public int32 Next();
}
)";

	static const char* kCode =
		R"(include "counter.ch"

int32 Counter::Next() {
	value = value + 1;
	return value;
}
)";

	static const char* kMain =
		R"(include "counter.ch"

int32 main() {
	Counter c;
	c.Next();
	return c.Next();
}
)";

	static int CountIn(const Core::Vector<C4LSP::Protocol::Location>& locations, const std::string& file)
	{
		int count = 0;
		for (const auto& location : locations)
			if (Testing::Lsp::Contains(location.uri, file))
				++count;
		return count;
	}

	void LspQualifiedDefinitionsTest::TestReferencesSearchEveryFile()
	{
		C4LSP::Server::CompilationSession session;
		session.RegisterInMemorySource("counter.ch", kHeader);
		session.RegisterInMemorySource("counter.cf", kCode);
		session.RegisterInMemorySource("main.cf", kMain);
		session.Recompile();
		auto call = Testing::Lsp::PositionOf(kMain, "Next", 1);

		auto uses = References::At(session, Testing::Lsp::UriFor("main.cf"), call);
		TEST_ASSERT_EQ(uses.Count(), 2u);
		TEST_ASSERT_EQ(CountIn(uses, "main.cf"), 2);

		// With the declarations: the header's, the out-of-line definition's, then the two calls.
		auto all = References::At(session, Testing::Lsp::UriFor("main.cf"), call, true);
		TEST_ASSERT_EQ(all.Count(), 4u);
		TEST_ASSERT_EQ(CountIn(all, "counter.ch"), 1);
		TEST_ASSERT_EQ(CountIn(all, "counter.cf"), 1);
		TEST_ASSERT_EQ(CountIn(all, "main.cf"), 2);

		// The uses inside the definition's file are found from another file too.
		auto value = References::At(session, Testing::Lsp::UriFor("counter.cf"), Testing::Lsp::PositionOf(kCode, "value", 1));
		TEST_ASSERT_EQ(CountIn(value, "counter.cf"), 3);
		TEST_PASSED();
	}
} // namespace Testing
