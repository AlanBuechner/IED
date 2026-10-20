#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// TypeHandle's capability queries over real model types.
	CLASS(GROUP = Testing) TypeHandleTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TypeHandleTest);

		TEST_FUNC(Semantic.TypeHandle)
		void ScalarsEnumsAndFunctionPointersHoldNoPointers();
		TEST_FUNC(Semantic.TypeHandle)
		void PointersAndReferencesAnywhereInATypeHoldPointers();
		TEST_FUNC(Semantic.TypeHandle)
		void StaticFieldsDoNotCount();
	};
} // namespace Testing
