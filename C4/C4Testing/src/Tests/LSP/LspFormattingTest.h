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
		void TestCrlfFileFormatsKeepingItsBreaks();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestCompoundOperatorsStayGluedAndSpacedCorrectly();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestSemicolonGluesLeftAndSpacesRight();

		// An operator after `)` or `]` is binary (`f(a) && b`), and a `(` after an operator or a keyword groups
		// rather than calls (`x = (a)`, `return (a)`, `catch (e)`): neither may be glued to its neighbor (BUG-411).
		TEST_FUNC(Lsp.Features.Formatting)
		void TestOperatorsAfterClosersAndParensAfterOperators();

		TEST_FUNC(Lsp.Features.Formatting)
		void TestLambdaReturnArrowKeepsItsSpaces();

		// A body starting with `(` after a control header's `)` keeps its space: `if (x) ((T*)p)->m = 1;` is no call of
		// the condition (BUG-424).
		TEST_FUNC(Lsp.Features.Formatting)
		void TestParenthesizedBodyAfterControlHeaderKeepsItsSpace();

		// `a > (b)` keeps its spaces; a template's `Make<T>(` stays tight (BUG-425).
		TEST_FUNC(Lsp.Features.Formatting)
		void TestGreaterThanBeforeParenIsNoTemplateCall();

		// A "\r\n" file keeps its breaks: the '\r' is no trailing whitespace, so code lines kept it as comment lines did
		// not lose it - formatting left files with both breaks (BUG-426).
		TEST_FUNC(Lsp.Features.Formatting)
		void TestCrlfBreaksAreKept();

		// The body of a brace-less `if`/`for`/`while`/`else` is indented once more than its header, a `{` alone under a
		// header sits at the header's indentation, and a lambda passed to a call indents its body once (BUG-411).
		TEST_FUNC(Lsp.Features.Formatting)
		void TestBraceLessBodiesAndLambdasIndentOnce();

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
