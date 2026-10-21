#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The compiled-script cache (Script/ScriptCache, docs/runtime/04 section 4.6): which inputs name an entry, what an
	// entry keeps, and which entries a write replaces.
	CLASS(GROUP = Testing) ScriptCacheTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ScriptCacheTest);

		TEST_FUNC(Script.Cache)
		void TheSameInputsNameTheSameEntry();
		TEST_FUNC(Script.Cache)
		void EveryInputChangesTheEntry();
		TEST_FUNC(Script.Cache)
		void AnEntryIsNamedForItsScriptAndLevel();
		TEST_FUNC(Script.Cache)
		void AnEntryReadsBackWhole();
		TEST_FUNC(Script.Cache)
		void ADamagedOrMissingEntryReadsAsNone();
		TEST_FUNC(Script.Cache)
		void AWriteReplacesOnlyItsScriptsEntryAtItsLevel();
		TEST_FUNC(Script.Cache)
		void AnUnreadableScriptFileHasNoEntry();
		TEST_FUNC(Script.Cache)
		void ABuildTagSharesTheEntry();
	};
} // namespace Testing
