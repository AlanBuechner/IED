#pragma once

#include "UnitTesting/TestBase.h"
#include "Reflection.h"
#include "ReflectionEvents.h"

namespace Testing
{
	CLASS(GROUP = Testing) ScriptingIDTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ScriptingIDTest);

		TEST_FUNC(ScriptingID)
		void TestConsistentHashing();

		TEST_FUNC(ScriptingID)
		void TestRegistryFilteringByLanguageID();

		TEST_FUNC(ScriptingID)
		void TestHasScriptingLanguage();

		TEST_FUNC(ScriptingID)
		void TestGetClassesByLanguageID();
	};
}
