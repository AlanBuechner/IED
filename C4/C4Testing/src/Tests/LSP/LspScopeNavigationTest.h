#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Names declared by a nested scope - a lambda, a range-for statement - and what a declaration's own name leads to.
	CLASS(GROUP = Testing) LspScopeNavigationTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspScopeNavigationTest);

		// BUG-403: a lambda's parameters and the names in its body, which hover answered with the enclosing function.
		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestLambdaParametersAndBodyResolve();

		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestCallsInALambdaAreReferences();

		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestLambdaCapturesNameTheirVariable();

		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestRenameReachesLambdaCaptures();

		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestInitCapturesDeclareAVariable();

		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestALocalUsedOnlyInALambdaIsUsed();

		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestLambdaBodiesAreColored();

		// BUG-407: a range-for variable's declaration is its name, not the whole statement.
		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestRangeForVariableIsItsName();

		// BUG-406: go-to-definition on a declaration's own name: a definition's declaration, the type qualifying it.
		TEST_FUNC(Lsp.Navigation.Scopes)
		void TestDefinitionFromADeclarationsOwnName();
	};
} // namespace Testing
