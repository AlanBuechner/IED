#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Importing the host's inline bodies into a script (Script/InlineImport, docs/runtime/04 section 3.3): the index of
	// where each body is, read once for every compile; the bodies a script reaches within kInlineImportDepth calls, and
	// only those; a chunk that does not read.
	CLASS(GROUP = Testing) InlineImportTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::InlineImportTest);

		TEST_FUNC(Script.InlineImport)
		void TheIndexLocatesEveryBody();
		TEST_FUNC(Script.InlineImport)
		void TheLastChunkHoldingABodyGivesIt();
		TEST_FUNC(Script.InlineImport)
		void AScriptTakesTheBodiesItReachesTwoCallsDeep();
		TEST_FUNC(Script.InlineImport)
		void ScriptsShareOneIndex();
		TEST_FUNC(Script.InlineImport)
		void AScriptCallingNoBodyTakesNone();
		TEST_FUNC(Script.InlineImport)
		void ADamagedChunkIsSaid();
	};
} // namespace Testing
