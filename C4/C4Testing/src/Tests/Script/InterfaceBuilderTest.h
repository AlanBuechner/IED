#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A host's interface from a compiled program (Script/InterfaceBuilder, docs/runtime/04 section 3.2).
	CLASS(GROUP = Testing) InterfaceBuilderTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::InterfaceBuilderTest);

		TEST_FUNC(Script.InterfaceBuilder)
		void EverySourceIsAFileInUnitOrder();
		TEST_FUNC(Script.InterfaceBuilder)
		void LibraryFilesAndThePreludeAreMarked();
		TEST_FUNC(Script.InterfaceBuilder)
		void TextsAreInterfaceTexts();
		TEST_FUNC(Script.InterfaceBuilder)
		void TheBuildIdFollowsTheContent();
		TEST_FUNC(Script.InterfaceBuilder)
		void PreludeFunctionsAreListed();
	};
} // namespace Testing
