#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Splicing a unit's includes into its tree (CompilerInstance::MergeIncludes): the language server's step, which a
	// build does not take.
	CLASS(GROUP = Testing) IncludeMergeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::IncludeMergeTest);

		TEST_FUNC(Compiler.IncludeMerge)
		void AnalyzingLeavesEachTreeItsOwn();
		TEST_FUNC(Compiler.IncludeMerge)
		void MergingSplicesTheIncludedDeclarations();
	};
} // namespace Testing
