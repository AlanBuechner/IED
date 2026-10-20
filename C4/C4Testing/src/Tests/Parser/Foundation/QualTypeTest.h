#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) QualTypeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::QualTypeTest);

		TEST_FUNC(Parser.Foundation.QualType.Basic)
		void TestPlainType();

		TEST_FUNC(Parser.Foundation.QualType.Basic)
		void TestLeadingConst();

		TEST_FUNC(Parser.Foundation.QualType.Indirection)
		void TestPointer();

		TEST_FUNC(Parser.Foundation.QualType.Indirection)
		void TestConstPointer();

		TEST_FUNC(Parser.Foundation.QualType.Complex)
		void TestPointerToConst();

		TEST_FUNC(Parser.Foundation.QualType.Indirection)
		void TestGCRef();

		TEST_FUNC(Parser.Foundation.QualType.Indirection)
		void TestCPPRef();

		TEST_FUNC(Parser.Foundation.QualType.Indirection)
		void TestArray();

		TEST_FUNC(Parser.Foundation.QualType.Complex)
		void TestComplexChain();

		TEST_FUNC(Parser.Foundation.QualType.Complex)
		void TestQualifiedNameWithTemplateArgs();

		TEST_FUNC(Parser.Foundation.QualType.Complex)
		void TestFullComplex();

		TEST_FUNC(Parser.Foundation.QualType.Negative)
		void TestNonTypeTokenReturnsNullptr();

		TEST_FUNC(Parser.Foundation.QualType.ErrorRecovery)
		void TestMissingTypeNameAfterConst();
	};
}
