#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// One server for every program of a workspace (docs/tools/03 "One server for every program"): the programs the options
	// files describe, each document analyzed in a program that compiles it, as few analyses as the open documents need,
	// and queries about a declaration answered by every program that reaches it.
	CLASS(GROUP = Testing) LspProgramsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspProgramsTest);

		// The owner's sandbox_module.ch: a module of no program of its own was analyzed alone, its includes unresolved.
		TEST_FUNC(Lsp.Programs)
		void TestAModuleIsAnalyzedInAProgramThatCompilesIt();

		TEST_FUNC(Lsp.Programs)
		void TestOneAnalysisServesTheDocumentsOfEveryProjectItHolds();

		TEST_FUNC(Lsp.Programs)
		void TestReferencesReachEveryProgram();

		// Every override of a virtual method, in the projects of other programs too.
		TEST_FUNC(Lsp.Programs)
		void TestImplementationsReachEveryProgram();

		TEST_FUNC(Lsp.Programs)
		void TestIncomingCallsAndSubtypesReachEveryProgram();

		TEST_FUNC(Lsp.Programs)
		void TestRenameReachesEveryProgram();

		TEST_FUNC(Lsp.Programs)
		void TestWorkspaceSymbolsOfEveryProgram();

		// IED sends the programs again when an options file changes (initializationOptionsFiles).
		TEST_FUNC(Lsp.Programs)
		void TestProgramsArriveWithAConfigurationChange();

		TEST_FUNC(Lsp.Programs)
		void TestMalformedProgramsAreLeftOut();

		// The planner, honest: as few programs as hold the documents.
		TEST_FUNC(Lsp.Programs.Plan)
		void TestAssignDocumentsCoversWithFewPrograms();

		TEST_FUNC(Lsp.Programs.Plan)
		void TestProgramsToConsultReachWhatTheAskedLacks();
	};
}
