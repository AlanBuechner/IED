#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Multiple return values (D-34) as the parser builds them: return list types, value lists, destructuring
	// statements, and their disambiguation from annotations and lambdas.
	CLASS(GROUP = Testing) ReturnListTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ReturnListTest);

		TEST_FUNC(Parser.ReturnLists)
		void ReturnTypeIsAList();
		TEST_FUNC(Parser.ReturnLists)
		void AnnotationsBeforeAList();
		TEST_FUNC(Parser.ReturnLists)
		void AnnotationIsNotAList();
		TEST_FUNC(Parser.ReturnLists)
		void FunPtrReturnsAList();
		TEST_FUNC(Parser.ReturnLists)
		void PackInAList();
		TEST_FUNC(Parser.ReturnLists)
		void ValueListExpression();
		TEST_FUNC(Parser.ReturnLists)
		void LambdaReturnsAList();
		TEST_FUNC(Parser.ReturnLists)
		void DestructuringElementKinds();
		TEST_FUNC(Parser.ReturnLists)
		void ComparisonIsNoDestructuring();
	};
} // namespace Testing
