#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Annotation declarations (docs/language/12 section 4, D-87): `annotation Range(double min, double max) on field;` declares
	// [Range], its parameters the arguments, its places where it goes and its overloads the forms a use may take; a use names
	// one as any name is named, from its namespace out, and is checked against it where it is written; an undeclared one is a
	// warning, and reflection registers the arguments as the declaration takes them, under its qualified name.
	CLASS(GROUP = Testing) DeclaredAnnotationsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DeclaredAnnotationsTest);

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void FoundByTheirQualifiedName();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AUseNamesOneAsAnyNameIsNamed();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AnInnerOneHidesAnOuterOne();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void ANamespacesOneIsNotSeenOutsideIt();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AMisspeltOneIsLookedUpWhereItIsWritten();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AMisspeltOneIsAnError();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void TwoSwappedNeighborsAreOneEdit();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void EveryKindOfDeclarationIsChecked();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AnUndeclaredOneIsAWarning();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void TheClosestIsNamed();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void TheArgumentCountIsChecked();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void EachArgumentsKindIsChecked();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AnIntegerMustFitItsParameter();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AnEnumParameterTakesItsEnumerators();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void OverloadsAreChosenByTheirArguments();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void ReflectionRegistersWhatTheDeclarationTakes();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void APackTakesAnyNumber();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void PlacesAreChecked();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void ADeclarationThatDeclaresNothingIsAnError();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void ItsSyntaxErrorsAreSaidOnce();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void AnnotationIsStillAName();

		TEST_FUNC(Semantic.DeclaredAnnotations)
		void ADeclarationIsNoFunction();
	};
}
