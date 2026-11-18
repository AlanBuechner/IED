#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// [annotation] declarations (docs/language/12 section 4): a struct or class marked [annotation] declares the annotation of its
	// name; a near miss of a declared one is an error where it is written, and [annotation] goes on a struct or class alone.
	CLASS(GROUP = Testing) DeclaredAnnotationsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DeclaredAnnotationsTest);

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void FoundByTheirOwnName();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AMisspeltOneIsAnError();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void EveryKindOfDeclarationIsChecked();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void NamesFarFromEveryDeclaredOneStayFree();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void TheClosestIsNamed();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void OnlyAStructOrClassDeclaresOne();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void ItTakesNoArguments();
	};
}
