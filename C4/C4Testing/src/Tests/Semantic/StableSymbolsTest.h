#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Symbols follow from the program, not from how far the compiler had got (docs/runtime/04 section 4.1): a
	// template instance is named for its arguments, a lambda for the code that writes it, a compiler-written
	// function for its type, so a script names what its host made exactly as the host did.
	CLASS(GROUP = Testing) StableSymbolsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::StableSymbolsTest);

		TEST_FUNC(Semantic.StableSymbols)
		void InstanceSymbolsFollowTheirArguments();
		TEST_FUNC(Semantic.StableSymbols)
		void InstanceSymbolsDoNotDependOnOrder();
		TEST_FUNC(Semantic.StableSymbols)
		void QualifiedArgumentsAreEscaped();
		TEST_FUNC(Semantic.StableSymbols)
		void LambdaSymbolsFollowTheirFunction();
		TEST_FUNC(Semantic.StableSymbols)
		void LifecycleSymbolsFollowTheirType();
		TEST_FUNC(Semantic.StableSymbols)
		void FileStaticGlobalsHaveTheirOwnSymbols();
		TEST_FUNC(Semantic.StableSymbols)
		void CloseFloatArgumentsAreDistinctInstances();
	};
} // namespace Testing
