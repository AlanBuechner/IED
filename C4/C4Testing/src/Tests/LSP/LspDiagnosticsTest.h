#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Where the editor shows what the compiler found (Features/Diagnostics) and where a position the editor names lands.
	CLASS(GROUP = Testing) LspDiagnosticsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspDiagnosticsTest);

		// BUG-446: on a line with a macro use, a diagnostic's column was the expanded text's: the squiggle sat inside the
		// macro's name, 28 columns before the error.
		TEST_FUNC(Lsp.Features.Diagnostics)
		void TestAnErrorAfterAMacroUseIsWhereItIsWritten();

		// The other way: a position the editor names after a macro use reaches the token written there.
		TEST_FUNC(Lsp.Features.Diagnostics)
		void TestAPositionAfterAMacroUseFindsItsToken();

		// BUG-433: a file-scope variable's initializer was never lowered, so a name nothing declares in it was not reported,
		// while C4.exe refuses the file.
		TEST_FUNC(Lsp.Features.Diagnostics)
		void TestAnUnknownNameInAGlobalsStartIsReported();
	};
} // namespace Testing
