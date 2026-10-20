#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Protocol/TextDocumentSync.h"

namespace Testing
{
	// Exercises folding LSP document edits (full and incremental) into a complete buffer, which is
	// what the compiler needs to re-parse. Covers inserts, deletes, replacements across lines and
	// multiple sequential changes.
	CLASS(GROUP = Testing) TextDocumentSyncTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TextDocumentSyncTest);

		TEST_FUNC(Lsp.TextSync)
		void TestIncrementalInsertAtStart();

		TEST_FUNC(Lsp.TextSync)
		void TestIncrementalDeleteRange();

		TEST_FUNC(Lsp.TextSync)
		void TestIncrementalReplaceAcrossLines();

		TEST_FUNC(Lsp.TextSync)
		void TestMultipleSequentialChanges();

		TEST_FUNC(Lsp.TextSync)
		void TestReplaceWholeBufferViaFullRange();

	private:
		C4LSP::Protocol::TextContentChangeEvent MakeChange(uint32 startLine, uint32 startChar,
			uint32 endLine, uint32 endChar, const std::string& text);
	};
} // namespace Testing
