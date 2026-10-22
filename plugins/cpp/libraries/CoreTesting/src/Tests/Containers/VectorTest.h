#pragma once

#include "UnitTesting/TestBase.h"
#include "Core/Vector.h"

namespace Testing
{
	CLASS(GROUP = Testing) VectorTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::VectorTest);

#pragma region Constructors

		TEST_FUNC(Vector.Constructors)
		void TestDefaultConstructor();

		TEST_FUNC(Vector.Constructors)
		void TestCopyConstructor();

		TEST_FUNC(Vector.Constructors)
		void TestMoveConstructor();

		TEST_FUNC(Vector.Constructors)
		void TestCountConstructor();

		TEST_FUNC(Vector.Constructors)
		void TestCountValueConstructor();

		TEST_FUNC(Vector.Constructors)
		void TestInitializerListConstructor();

#pragma endregion

#pragma region Accessors

		TEST_FUNC(Vector.Accessors)
		void TestOperatorIndex();

		TEST_FUNC(Vector.Accessors)
		void TestFront();

		TEST_FUNC(Vector.Accessors)
		void TestBack();

		TEST_FUNC(Vector.Accessors)
		void TestData();

#pragma endregion

#pragma region Existing

		TEST_FUNC(Vector.Mutations)
		void TestPushBackElement();

		TEST_FUNC(Vector.Mutations)
		void TestResizeUpAndDown();

		TEST_FUNC(Vector.Mutations)
		void TestSortAscending();

#pragma endregion
	};
}
