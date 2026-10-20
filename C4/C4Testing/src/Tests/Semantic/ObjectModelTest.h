#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// The object model bricks (docs/compiler/04, D-08..D-13): MSVC layout of class bases and interfaces,
	// virtual slots and vptr locations, dispatch and final overriders, base class lists, MSVC type names
	// and the member / extension calling conventions, plus the emitted vtables, thunks and RTTI.
	CLASS(GROUP = Testing) ObjectModelTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ObjectModelTest);

		// --- layout -------------------------------------------------------------------
		TEST_FUNC(Semantic.ObjectModel)
		void PolymorphicBasesComeFirst();
		TEST_FUNC(Semantic.ObjectModel)
		void OwnVptrWithoutPolymorphicBase();

		// --- virtual slots ------------------------------------------------------------
		TEST_FUNC(Semantic.ObjectModel)
		void SlotsKeepBaseOrderAndSplitOverloads();
		TEST_FUNC(Semantic.ObjectModel)
		void EachInterfaceHasItsOwnVptr();
		TEST_FUNC(Semantic.ObjectModel)
		void InterfaceCallsDispatchThroughTheirVptr();

		// --- hierarchy ----------------------------------------------------------------
		TEST_FUNC(Semantic.ObjectModel)
		void RepeatedBaseIsAmbiguous();

		// --- names and calling conventions -------------------------------------------
		TEST_FUNC(Semantic.ObjectModel)
		void MsvcNamesSpellTypes();
		TEST_FUNC(Semantic.ObjectModel)
		void MsvcNamesBackReferenceRepeatedNames();
		TEST_FUNC(Semantic.ObjectModel)
		void SignaturesFollowMsvcConventions();

		// --- emitted tables -----------------------------------------------------------
		TEST_FUNC(Semantic.ObjectModel)
		void VtablesThunksAndRttiAreEmitted();
	};
} // namespace Testing
