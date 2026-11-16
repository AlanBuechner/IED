#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The compiled-script cache (Script/ScriptCache, docs/runtime/04 section 4.6): which inputs name an entry - the files the
	// script's project reaches, without their blank lines, not the host's build id - what an entry keeps, which entries a
	// write replaces, and the assumptions an entry is used under (Script/HostAssumptions).
	CLASS(GROUP = Testing) ScriptCacheTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ScriptCacheTest);

		TEST_FUNC(Script.Cache)
		void TheSameInputsNameTheSameEntry();
		TEST_FUNC(Script.Cache)
		void EveryInputChangesTheEntry();
		TEST_FUNC(Script.Cache)
		void AHostBuildOfTheSameFilesKeepsTheEntry();
		TEST_FUNC(Script.Cache)
		void ABodyEditKeepsTheEntry();
		TEST_FUNC(Script.Cache)
		void AnAssertionThatMovesChangesTheEntry();
		TEST_FUNC(Script.Cache)
		void AFileOutOfReachKeepsTheEntry();
		TEST_FUNC(Script.Cache)
		void AnAddedFileChangesTheEntry();
		TEST_FUNC(Script.Cache)
		void ACollidedSymbolChangesTheEntry();
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
		TEST_FUNC(Script.Cache)
		void AssumptionsHoldWhileTheHostAnswersAlike();
		TEST_FUNC(Script.Cache)
		void AFilesDigestIsItsTextWithoutBlankLines();
	};
} // namespace Testing
