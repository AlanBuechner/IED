#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Model/QueryResult.h"

namespace Testing
{
	CLASS(GROUP = Testing) ProgramModelTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ProgramModelTest);

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryTypeSuccessAndAliasUnwrap();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryTypeNotFound();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQuerySymbolKind();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryFunctionSuccess();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryFunctionNotCallableWhenNameIsAType();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryFunctionNotFound();

		TEST_FUNC(Semantic.ProgramModel)
		void TestInvalidModelReportsNotFound();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryConstantEnumMemberAndConstVar();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryConstantNotAValueForNonConst();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryConstantNotFound();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryMemberFieldAndMethod();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryMemberInheritedMostDerivedFirst();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryMemberNotAnObjectType();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryMemberNotFound();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryOperatorBinaryLeftReceiver();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryOperatorNeverSwapsOperands();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryOperatorUnary();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryOperatorNotFoundForPrimitives();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryOperatorCompoundInPlaceAndFallback();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryVariableConstAndStatic();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryVariableNotAVariable();

		TEST_FUNC(Semantic.ProgramModel)
		void TestLayoutSizeOfAndAlignOf();

		TEST_FUNC(Semantic.ProgramModel)
		void TestElementOfArrayPointerAndScalar();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQualifiedTypeAndFunction();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQualifiedConstantFolds();

		TEST_FUNC(Semantic.ProgramModel)
		void TestQueryInstantiationReturnsConcreteType();
	};
} // namespace Testing
