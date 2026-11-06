#include "LspDiagnosticsTest.h"
#include "LspTestSupport.h"
#include "Features/Diagnostics.h"
#include "Features/Definition.h"
#include <string>

using Testing::Lsp::MakeSession;
using Testing::Lsp::PositionOf;
using Testing::Lsp::UriFor;

namespace Testing
{
	namespace
	{
		// Two uses of a long macro, then an error and a use of a local on the same line.
		const char* kMacroLine =
			"#define LONG_MACRO_NAME 2\n"
			"\n"
			"int32 main()\n"
			"{\n"
			"\tint32 a = LONG_MACRO_NAME + LONG_MACRO_NAME; int64 b = \"text\";\n"
			"\tint32 c = LONG_MACRO_NAME; int32 d = c;\n"
			"\treturn a + d;\n"
			"}\n";
	}

	void LspDiagnosticsTest::TestAnErrorAfterAMacroUseIsWhereItIsWritten()
	{
		auto session = MakeSession("macro_line.cf", kMacroLine);
		const auto diagnostics = C4LSP::Features::Diagnostics::ForDocument(session, UriFor("macro_line.cf"));
		TEST_ASSERT_EQ(diagnostics.Count(), (uint32)1);
		const C4LSP::Protocol::Position written = PositionOf(kMacroLine, "int64 b");
		TEST_ASSERT_EQ(diagnostics[0].range.start.line, written.line);
		TEST_ASSERT_EQ(diagnostics[0].range.start.character, written.character);
		TEST_ASSERT_EQ(diagnostics[0].range.end.character, written.character + 5); // the squiggle under `int64`
		TEST_PASSED();
	}

	void LspDiagnosticsTest::TestAPositionAfterAMacroUseFindsItsToken()
	{
		auto session = MakeSession("macro_line.cf", kMacroLine);
		const C4LSP::Protocol::Position use = PositionOf(kMacroLine, "= c;");
		const auto locations = C4LSP::Features::Definition::At(session, UriFor("macro_line.cf"), { use.line, use.character + 2 });
		TEST_ASSERT_EQ(locations.Count(), (uint32)1);
		const C4LSP::Protocol::Position declared = PositionOf(kMacroLine, "c = LONG_MACRO_NAME");
		TEST_ASSERT_EQ(locations[0].range.start.line, declared.line);
		TEST_ASSERT_EQ(locations[0].range.start.character, declared.character);
		TEST_PASSED();
	}
} // namespace Testing
