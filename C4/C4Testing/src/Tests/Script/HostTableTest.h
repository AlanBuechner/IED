#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What a script host's image carries for scripts (Script/HostTable, Script/InlineBodies, docs/runtime/04
	// section 3): the symbol table, the thread_local accessors, and the bodies scripts may inline.
	CLASS(GROUP = Testing) HostTableTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::HostTableTest);

		TEST_FUNC(Script.HostTable)
		void AccessorsGiveTheCallingThreadsAddress();
		TEST_FUNC(Script.HostTable)
		void SymbolsListWhatAScriptMayName();
		TEST_FUNC(Script.HostTable)
		void SymbolsLeaveOutPrivateAndThreadStorage();
		TEST_FUNC(Script.HostTable)
		void TableReplacesTheDeclarationInOrder();
		TEST_FUNC(Script.HostTable)
		void AMissingSymbolIsAnError();
		TEST_FUNC(Script.HostTable)
		void SmallBodiesAreKept();
		TEST_FUNC(Script.HostTable)
		void LargeBodiesAndThreadStorageAreLeftOut();
		TEST_FUNC(Script.HostTable)
		void ConstantsTravelWithTheirBodies();
		TEST_FUNC(Script.HostTable)
		void BodiesComeInChunksWithTheirConstants();
		TEST_FUNC(Script.HostTable)
		void NoBodiesWithoutSymbols();
	};
} // namespace Testing
