#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A file's declaration fingerprint (Compiler/DeclarationFingerprint, docs/compiler/07 "Exact invalidation"): an edit
	// inside the body of a function only its own lowering reads keeps it; every other edit changes it.
	CLASS(GROUP = Testing) DeclarationFingerprintTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DeclarationFingerprintTest);

		TEST_FUNC(Compiler.DeclarationFingerprint)
		void AStatementInAPlainBodyKeepsIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void AMethodsBodyKeepsIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void CommentsAndLinesKeepIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void ASignatureChangesIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void AFieldChangesIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void AnIncludeChangesIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void AConstantsValueChangesIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void ATemplatesBodyChangesIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void AClassTemplatesMethodBodyChangesIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void ACompileTimeBodyChangesIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void AnInlineBodyChangesIt();
		TEST_FUNC(Compiler.DeclarationFingerprint)
		void AnAnnotationChangesIt();
	};
} // namespace Testing
