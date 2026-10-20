#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Model/SymbolTable.h"
#include "Model/QueryResult.h"

namespace Testing
{
	// Simulated lowering (doc 06): drive the ProgramModel's RAII scope API exactly the way HIR
	// lowering will, WITHOUT a backend - push a function scope, register each declaration as it is
	// reached, push/pop a block scope for every structured construct, and assert what name lookup
	// returns at each point. This locks the contract the whole rework depends on: scopes are layers
	// pushed and popped by object lifetime (leaks impossible), locals shadow globals, function-local
	// types and functions exist only in their scope, and every query a lower system needs succeeds.
	// It is also the live check that the API stays simple: if lowering one body requires more than
	// PushScope / RegisterLocal / RegisterLocalType / RegisterLocalFunction / Query*, this suite gets
	// ugly and tells us the model is missing a verb.
	CLASS(GROUP = Testing) SimulatedLoweringTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::SimulatedLoweringTest);

		// --- Scope mechanics (doc 06 RAII) -------------------------------------------
		TEST_FUNC(Semantic.SimulatedLowering)
		void ScopePushPopAndLocalShadowsGlobal();

		TEST_FUNC(Semantic.SimulatedLowering)
		void NestedBlockDoesNotLeakIntoSibling();

		TEST_FUNC(Semantic.SimulatedLowering)
		void InnermostScopeWinsThenOuterRestored();

		TEST_FUNC(Semantic.SimulatedLowering)
		void RedeclarationInSameBlockIsRejected();

		// --- Function-local declarations (doc 06: dynamic registration) ---------------
		TEST_FUNC(Semantic.SimulatedLowering)
		void LocalStructVisibleOnlyInsideItsScope();

		TEST_FUNC(Semantic.SimulatedLowering)
		void LocalFunctionQueryableOnlyInsideItsScope();

		TEST_FUNC(Semantic.SimulatedLowering)
		void LocalTypeUsedBeforeDeclaredIsNotFound();

		// --- AST-driven: lower a real body statement by statement ---------------------
		TEST_FUNC(Semantic.SimulatedLowering)
		void LowerRealBodyTracksLocalsAcrossBlocks();

		// --- Completeness: every query a lower system needs succeeds on one body -------
		TEST_FUNC(Semantic.SimulatedLowering)
		void LoweringGetsEveryFactItNeeds();
	};
} // namespace Testing
