#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Const layers and const methods (D-30, D-31; BUG-007, BUG-025, BUG-084): what each `const` in a type
	// qualifies, which declared entities are read-only, and how a receiver's access picks among a method and
	// its const twin.
	CLASS(GROUP = Testing) ConstLayersTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ConstLayersTest);

		TEST_FUNC(Semantic.ConstLayers)
		void EachConstQualifiesTheLayerAfterIt();

		TEST_FUNC(Semantic.ConstLayers)
		void DeclaredEntitiesTakeTheOutermostConst();

		TEST_FUNC(Semantic.ConstLayers)
		void TrailingConstIsAnError();

		TEST_FUNC(Semantic.ConstLayers)
		void ConstMethodsAreRecordedAndMangledApart();

		TEST_FUNC(Semantic.ConstLayers)
		void ReceiverAccessSelectsTheTwin();

		TEST_FUNC(Semantic.ConstLayers)
		void OutOfLineDefinitionMatchesConstness();
	};
} // namespace Testing
