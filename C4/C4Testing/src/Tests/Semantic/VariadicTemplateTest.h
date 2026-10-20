#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Variadic templates, specialization and target-typed deduction through the model's queries
	// (docs/language/08 5-7, D-17, D-19, D-20).
	CLASS(GROUP = Testing) VariadicTemplateTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::VariadicTemplateTest);

		TEST_FUNC(Semantic.Templates.Variadic)
		void PackBindsTrailingArguments();
		TEST_FUNC(Semantic.Templates.Variadic)
		void ExpansionInTemplateArguments();
		TEST_FUNC(Semantic.Templates.Variadic)
		void SizeOfPackIsAConstant();
		TEST_FUNC(Semantic.Templates.Variadic)
		void DeducesPackFromCallArguments();
		TEST_FUNC(Semantic.Templates.Variadic)
		void DeducesPackThroughInstance();
		TEST_FUNC(Semantic.Templates.Specialization)
		void SpecializationPeelsPack();
		TEST_FUNC(Semantic.Templates.Specialization)
		void FixedPatternBeatsTrailingExpansion();
		TEST_FUNC(Semantic.Templates.Specialization)
		void TopLevelConstIsNotPartOfAPattern();
		TEST_FUNC(Semantic.Templates.Specialization)
		void ExplicitFunctionSpecializationHasItsOwnBody();
		TEST_FUNC(Semantic.Templates.Deduction)
		void TemplateNameFindsInstanceForSignature();
	};
} // namespace Testing
