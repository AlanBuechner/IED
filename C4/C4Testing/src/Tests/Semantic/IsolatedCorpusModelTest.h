#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// ISOLATED CORPUS -> MODEL TESTS. Loads REAL .cf files from assets/IsolatedTest (the on-disk
	// feature/regression corpus) and validates that the ProgramModel produces ALL the information a
	// lower system needs: every named type resolves with computed layout, every function definition
	// has a lowering record carrying its body node, operator methods are queryable by their
	// [operator(...)] annotation, and out-of-line method definitions attach to their declaration.
	// This is the "take code from files like IsolatedTest" gate: real source in, complete model facts
	// out - independent of any hand-written test snippet.
	CLASS(GROUP = Testing) IsolatedCorpusModelTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::IsolatedCorpusModelTest);

		TEST_FUNC(Semantic.IsolatedCorpus)
		void RangeForFileProducesTypesAndBodies();

		TEST_FUNC(Semantic.IsolatedCorpus)
		void OperatorFileRecordsOperatorAnnotations();

		TEST_FUNC(Semantic.IsolatedCorpus)
		void InheritanceFileComputesLayoutAndVirtuals();

		TEST_FUNC(Semantic.IsolatedCorpus)
		void OutOfLineMethodAttachesBodyToDeclaration();

		TEST_FUNC(Semantic.IsolatedCorpus)
		void TemplateFileInstantiatesToConcreteLayouts();

		TEST_FUNC(Semantic.IsolatedCorpus)
		void NamespacedFileResolvesQualifiedTypesAndBodies();

		// Full-corpus sweep: every self-contained .cf in assets/IsolatedTest must build a COMPLETE
		// model (AnalyzeProgram true + valid). The handful of files that are not self-contained on
		// their own (GPU/env, native externs, multi-file, preprocessor semantics) are named below.
		TEST_FUNC(Semantic.IsolatedCorpus)
		void EverySelfContainedFileBuildsACompleteModel();

		// Full-corpus LOWERING sweep: for every self-contained .cf, lower all function bodies and
		// assert the diagnostic guarantee - a body either lowers completely or reports at least one
		// diagnostic. This locks "nothing fails silently" against the whole real corpus (the invariant
		// that was violated before LowerFunction guaranteed a diagnostic on every incomplete lowering).
		TEST_FUNC(Semantic.IsolatedCorpus)
		void EverySelfContainedFileLowersWithoutSilentFailures();
	};
} // namespace Testing
