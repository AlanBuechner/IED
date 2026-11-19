#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/AnnotationDeclarationNode.h"

namespace Testing
{
	// `annotation Name(parameters) on places;` (docs/language/12 section 4.1): `annotation` starts one only before a name.
	CLASS(GROUP = Testing) AnnotationDeclarationTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::AnnotationDeclarationTest);

		TEST_FUNC(Parser.AnnotationDeclaration)
		void ReadsTheNameParametersAndPlaces();

		TEST_FUNC(Parser.AnnotationDeclaration)
		void TheParenthesesAndPlacesMayBeLeftOut();

		TEST_FUNC(Parser.AnnotationDeclaration)
		void APlaceMayBeAKeyword();

		TEST_FUNC(Parser.AnnotationDeclaration)
		void AnnotationIsANameAnywhereElse();
	};
}
