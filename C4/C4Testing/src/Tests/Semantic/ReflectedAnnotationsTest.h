#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Model/Annotations (D-42): the annotations reflection keeps and how their arguments fold.
	CLASS(GROUP = Testing) ReflectedAnnotationsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ReflectedAnnotationsTest);

		TEST_FUNC(Semantic.ReflectedAnnotations)
		void ArgumentsFold();

		TEST_FUNC(Semantic.ReflectedAnnotations)
		void CompilerAnnotationsAreSkipped();

		TEST_FUNC(Semantic.ReflectedAnnotations)
		void NonConstantArgumentFails();

		TEST_FUNC(Semantic.ReflectedAnnotations)
		void DeclarationThenDefinition();

		TEST_FUNC(Semantic.ReflectedAnnotations)
		void NoAnnotations();
	};
}
