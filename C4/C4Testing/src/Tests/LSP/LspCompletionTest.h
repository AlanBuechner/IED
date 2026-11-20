#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Completion while typing (docs/tools/03 "Requests while it analyzes"): it answers from the last analysis, reading the
	// words around the caret from the editor's text and the scopes from the analysis at the place the edits since leave it;
	// a member access is read from the text through calls, indexes and members (`GetTransform()->`, `m_Body.Shape.`); a
	// method's body offers what an implicit `this` reaches, inherited members too.
	CLASS(GROUP = Testing) LspCompletionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspCompletionTest);

		// --- The place in the analyzed text ---
		TEST_FUNC(Lsp.Completion)
		void TestAPlaceBeforeOrAfterTheEditsIsTheSamePlace();

		TEST_FUNC(Lsp.Completion)
		void TestAPlaceInsideTheEditsIsWhereTheyStart();

		TEST_FUNC(Lsp.Completion)
		void TestAPlaceAfterADeletionMovesBack();

		// --- The chain a member access is made on ---
		TEST_FUNC(Lsp.Completion)
		void TestAChainReadsCallsIndexesAndMembers();

		TEST_FUNC(Lsp.Completion)
		void TestAChainReadsAQualifiedStart();

		TEST_FUNC(Lsp.Completion)
		void TestNoChainWhereNoValueIsAccessed();

		// --- What completion offers ---
		TEST_FUNC(Lsp.Completion)
		void TestMembersOfWhatACallReturns();

		TEST_FUNC(Lsp.Completion)
		void TestMembersThroughFieldsAndIndexes();

		TEST_FUNC(Lsp.Completion)
		void TestMembersThroughAnArrowOperator();

		TEST_FUNC(Lsp.Completion)
		void TestAnOutOfLineMethodOffersWhatThisReaches();

		TEST_FUNC(Lsp.Completion)
		void TestTextTypedSinceTheAnalysisCompletes();

		TEST_FUNC(Lsp.Completion)
		void TestAGlobalIsOfferedOnce();
	};
} // namespace Testing
