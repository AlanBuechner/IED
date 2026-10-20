#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Template arguments (docs/language/08, D-16..D-18, D-21): positional binding, defaults, deduction from
	// call arguments, overload sets of templates and functions, and templated extension methods, through the
	// model's public queries.
	CLASS(GROUP = Testing) TemplateDeductionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TemplateDeductionTest);

		TEST_FUNC(Semantic.Templates)
		void ArgumentsBindByPosition();
		TEST_FUNC(Semantic.Templates)
		void DefaultsMayNameEarlierParameters();
		TEST_FUNC(Semantic.Templates)
		void DeducesThroughParameterShapes();
		TEST_FUNC(Semantic.Templates)
		void DeducesTemplateInstancesAndTheirBases();
		TEST_FUNC(Semantic.Templates)
		void ConflictingDeductionsNameBothTypes();
		TEST_FUNC(Semantic.Templates)
		void WrittenArgumentsAreNotDeduced();
		TEST_FUNC(Semantic.Templates)
		void FunctionsBeatEqualTemplateInstances();
		TEST_FUNC(Semantic.Templates)
		void ExtensionTemplatesAttachToInstances();
	};
} // namespace Testing
