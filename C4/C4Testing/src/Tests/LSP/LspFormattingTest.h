#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Features/Formatting.h"
#include <string>
#include <vector>

namespace Testing
{
	// Exercises the conservative whitespace formatter (indentation, spacing around tokens) that backs
	// textDocument/formatting and rangeFormatting. These call Formatting::Format directly on raw text,
	// so no compiled session is needed - the whole point of a lexer-driven formatter is that it works
	// without a successful parse.
	CLASS(GROUP = Testing) LspFormattingTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspFormattingTest);

		TEST_FUNC(Lsp.Features.Formatting)
		void TestIndentationNormalizedToFourSpaces();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestBinaryOperatorSpacing();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestNoSpaceAfterOpenParenForCallsButSpaceForControl();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestCommentsAreNeverTouched();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestBlankLinesCollapsedToEmpty();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestTrailingWhitespaceStripped();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestIdempotentOnAlreadyFormattedText();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestGarbageStillOnlyChangesWhitespace();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestCrlfLineEndingsNormalizedToLf();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestCompoundOperatorsStayGluedAndSpacedCorrectly();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestSemicolonGluesLeftAndSpacesRight();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestUnterminatedStringAtEofCannotCorruptOffsets();

		// Space -> tab conversion (the "replace spaces with tabs for basic indentation" behavior):
		// a client that wants tabs (insertSpaces:false) gets its space-indented document re-indented
		// one tab per block level. These lock in that direction plus the safety contract that the
		// formatter only ever touches whitespace, never token text or ordering.
		TEST_FUNC(Lsp.Features.Formatting)
		void TestSpacesConvertedToTabsWhenClientWantsTabs();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestTabIndentationIdempotentUnderTabMode();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestMultiDimInitializerOnlyChangesLeadingWhitespace();

		// `public:`, `private:` and `protected:` sit at their class's indentation, one level out from the members they
		// label; a C#-style modifier on a member (`public void F();`) leaves the member where it is.
		TEST_FUNC(Lsp.Features.Formatting)
		void TestAccessLabelsSitAtTheirClassIndentation();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestAccessLabelOutsideAnyBlockStaysAtColumnZero();

	private:
		// Apply a set of TextEdits (0-based line/char, LSP semantics) to `text` and return the result.
		static std::string Apply(const std::string& text, const std::vector<C4LSP::Protocol::TextEdit>& edits);
	};
} // namespace Testing
