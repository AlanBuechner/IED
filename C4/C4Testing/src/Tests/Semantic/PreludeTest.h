#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The prelude (D-43): Bedrock/Prelude/prelude.ch, which every unit includes, declares the runtime's functions
	// with [link] and the _GUID type; a program declaration of the same file-scope name hides the prelude's.
	CLASS(GROUP = Testing) PreludeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PreludeTest);

		TEST_FUNC(Semantic.Prelude)
		void PreludeFunctionsAreLinkedBuiltins();
		TEST_FUNC(Semantic.Prelude)
		void EveryUnitSeesThePrelude();
		TEST_FUNC(Semantic.Prelude)
		void ProgramFunctionHidesThePreludeOne();
		TEST_FUNC(Semantic.Prelude)
		void ProgramTypeHidesThePreludeOne();
		TEST_FUNC(Semantic.Prelude)
		void NamespacedNamesDoNotHide();
		TEST_FUNC(Semantic.Prelude)
		void NoDefaultLibraryMeansNoPrelude();
	};
}
