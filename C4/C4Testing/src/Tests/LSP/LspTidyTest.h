#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Features/Tidy.h"
#include <string>
#include <vector>

namespace Testing
{
	// Exercises C4 Tidy, the clang-tidy-like static analysis pass: unused local variables,
	// self-assignments and tautological comparisons surfaced as non-error diagnostics through the
	// LSP publishDiagnostics pipeline. Tests call Tidy::ForDocument on compiled sessions (via the
	// shared MakeSession helper) so they validate the real AST + semantic-analysis path end to end,
	// plus one full-server test proves the initialize/didChangeConfiguration gating works over JSON-RPC.
	CLASS(GROUP = Testing) LspTidyTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspTidyTest);

		TEST_FUNC(Lsp.Features.Tidy)
		void TestUnusedLocalVariableReported();

		TEST_FUNC(Lsp.Features.Tidy)
		void TestUsedLocalNotReported();

		TEST_FUNC(Lsp.Features.Tidy)
		void TestGlobalVariableNeverReported();

		TEST_FUNC(Lsp.Features.Tidy)
		void TestMultiDeclaratorLineReportsOnlyUnusedNames();

		TEST_FUNC(Lsp.Features.Tidy)
		void TestSelfAssignmentReportedAsInformation();

		TEST_FUNC(Lsp.Features.Tidy)
		void TestTautologicalComparisonReported();

		TEST_FUNC(Lsp.Features.Tidy)
		void TestDifferentVariablesNotFlagged();

		TEST_FUNC(Lsp.Features.Tidy)
		void TestDisabledFeatureProducesNoDiagnostics();

		TEST_FUNC(Lsp.Features.Tidy)
		void TestParseErrorDocumentYieldsNoTidyFindings();
};
} // namespace Testing
