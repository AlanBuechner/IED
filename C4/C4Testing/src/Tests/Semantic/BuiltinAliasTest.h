#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// The built-in aliases `int`, `uint`, `char` (D-01, D-02): each IS its target primitive, which is also
	// what the LSP's built-in coloring checks.
	CLASS(GROUP = Testing) BuiltinAliasTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::BuiltinAliasTest);

		TEST_FUNC(Semantic.BuiltinAliases)
		void AliasesResolveToTheirTargets();
		TEST_FUNC(Semantic.BuiltinAliases)
		void AliasedVariablesShareOneType();
		TEST_FUNC(Semantic.BuiltinAliases)
		void UserTypesShadowNoAlias();
	};
} // namespace Testing
