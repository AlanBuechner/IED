#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// How the driver builds the C4 runtime (Compiler/RuntimeObjects.h).
	CLASS(GROUP = Testing) RuntimeObjectsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::RuntimeObjectsTest);

		TEST_FUNC(Compiler.Runtime)
		void SourcesCompileOptimizedWithWarningsAsErrors();
		TEST_FUNC(Compiler.Runtime)
		void CppSourcesCompileAsCpp20();
		TEST_FUNC(Compiler.Runtime)
		void ChangedBytesNameAnotherObject();
		TEST_FUNC(Compiler.Runtime)
		void AChangedHeaderRenamesEveryObject();
		TEST_FUNC(Compiler.Runtime)
		void ObjectNamesKeepTheSourcePath();
	};
} // namespace Testing
