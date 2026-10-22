#pragma once

#include "UnitTesting/TestBase.h"
#include "Core/ArgList.h"

namespace Testing
{
	CLASS(GROUP = Testing) ArgListTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ArgListTest);

		TEST_FUNC(ArgList)
		void TestGetArg();

		TEST_FUNC(ArgList)
		void TestGetArgValue();

		TEST_FUNC(ArgList)
		void TestGetArgFlag();

		TEST_FUNC(ArgList)
		void TestGetMissingArg();

		TEST_FUNC(ArgList)
		void TestDoubleDashAndSlash();

		TEST_FUNC(ArgList)
		void TestEmptyArgument();

	};
}
