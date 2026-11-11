#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// When the server analyzes a program and what a request waits for (docs/tools/03 "When a program is analyzed"): an
	// analysis is made again only when what it compiled changed - in the editor or on disk - a request waits for its own
	// program alone, and a search of the workspace's symbols answers from what analyses let go of left.
	CLASS(GROUP = Testing) LspAnalysesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspAnalysesTest);

		// The AI's outline of a file, or a tab the editor opens, cost a whole analysis each (6.8 s on Claymore's engine).
		TEST_FUNC(Lsp.Analyses)
		void TestOpeningOrClosingAnUnchangedFileAnalyzesNothing();

		TEST_FUNC(Lsp.Analyses)
		void TestClosingAnEditedDocumentAnalyzesItsFileAgain();

		// An edit in one program held a hover in another for up to 10 s.
		TEST_FUNC(Lsp.Analyses)
		void TestARequestWaitsForItsOwnProgramAlone();

		TEST_FUNC(Lsp.Analyses)
		void TestAnOutlineWaitsForItsOwnDocumentAlone();

		// BUG-449: a header something besides the editor changed left the open documents' diagnostics as they were.
		TEST_FUNC(Lsp.Analyses)
		void TestAFileChangedOnDiskIsAnalyzedAgain();

		// BUG-450: a syntax error in one file (every keystroke of an edit makes one) hid every other file's body errors.
		TEST_FUNC(Lsp.Analyses)
		void TestABrokenFileKeepsTheBodyErrorsOfTheOthers();

		TEST_FUNC(Lsp.Analyses)
		void TestASearchAnswersFromTheBackgroundIndex();

		TEST_FUNC(Lsp.Analyses)
		void TestASearchTakesAnOpenDocumentsSymbolsFromItsAnalysis();

		// BUG-447: a didChange's range was dropped, so a client sending incremental changes had its document spliced at 0:0.
		TEST_FUNC(Lsp.Analyses)
		void TestARangedChangeEditsWhereItSays();

		// BUG-452: a file wrapped in `#ifdef TEST` (every test file of Claymore) had no symbols: TEST was never defined.
		TEST_FUNC(Lsp.Analyses)
		void TestTestCodeIsAnalyzed();
	};
} // namespace Testing
