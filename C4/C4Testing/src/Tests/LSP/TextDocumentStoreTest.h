#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Protocol/TextDocumentStore.h"

namespace Testing
{
	// Exercises the open-document bookkeeping: full vs incremental change dispatch, version
	// tracking and close. This is the layer that decides whether a didChange carries the whole
	// text or a set of splices.
	CLASS(GROUP = Testing) TextDocumentStoreTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TextDocumentStoreTest);

		TEST_FUNC(Lsp.TextStore)
		void TestOpenTracksTextAndVersion();

		TEST_FUNC(Lsp.TextStore)
		void TestFullChangeReplacesWholeText();

		TEST_FUNC(Lsp.TextStore)
		void TestIncrementalChangeSplicesBuffer();

		TEST_FUNC(Lsp.TextStore)
		void TestCloseRemovesDocument();

		TEST_FUNC(Lsp.TextStore)
		void TestForEachVisitsAllOpenDocuments();
	};
} // namespace Testing
