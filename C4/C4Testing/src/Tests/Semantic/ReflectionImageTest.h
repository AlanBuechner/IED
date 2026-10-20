#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The reflection image codegen emits into the program's module (docs/runtime/02 section 1): its rows, the
	// constructor that registers it, and what it leaves out.
	CLASS(GROUP = Testing) ReflectionImageTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ReflectionImageTest);

		TEST_FUNC(Semantic.ReflectionImage)
		void ImageCountsTheProgramsEntities();
		TEST_FUNC(Semantic.ReflectionImage)
		void RegistersBeforeEveryOtherConstructor();
		TEST_FUNC(Semantic.ReflectionImage)
		void VariablesRecordTheirStorage();
		TEST_FUNC(Semantic.ReflectionImage)
		void InvokeThunksAreInternal();
		TEST_FUNC(Semantic.ReflectionImage)
		void CompilerWrittenFunctionsAreLeftOut();
		TEST_FUNC(Semantic.ReflectionImage)
		void AnnotationThatDoesNotFoldFailsTheBuild();
		TEST_FUNC(Semantic.ReflectionImage)
		void ModuleWithoutEntryPointLowers();
	};
} // namespace Testing
