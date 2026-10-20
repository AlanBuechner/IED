#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Reusing foreign objects (Interop/ForeignCache.h, BUG-285): an object stays while its source, the headers its
	// depfile lists, its flags and the compiler are unchanged.
	CLASS(GROUP = Testing) ForeignCacheTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ForeignCacheTest);

		TEST_FUNC(Compiler.ForeignCache)
		void DepfileInputsReadMakeSyntax();
		TEST_FUNC(Compiler.ForeignCache)
		void AnObjectIsCurrentUntilAnInputChanges();
		TEST_FUNC(Compiler.ForeignCache)
		void FlagsAndTheCompilerArePartOfTheKey();
		TEST_FUNC(Compiler.ForeignCache)
		void AnObjectWithoutItsRecordIsNotCurrent();
	};
} // namespace Testing
