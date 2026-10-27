#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A source's outline (Outline/SourceOutline, the C API's parse): its declarations with their kinds, names, places
	// and nesting, and its syntax and preprocessor diagnostics, from the macro preprocessor and the parser alone.
	CLASS(GROUP = Testing) SourceOutlineTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::SourceOutlineTest);

		TEST_FUNC(Outline.SourceOutline)
		void DeclarationsNestUnderWhatHoldsThem();
		TEST_FUNC(Outline.SourceOutline)
		void KindsOfTypesAndTheirMembers();
		TEST_FUNC(Outline.SourceOutline)
		void AMemberDefinedOutOfItsTypeKeepsItsWrittenName();
		TEST_FUNC(Outline.SourceOutline)
		void PlacesAreTheSourceAsWritten();
		TEST_FUNC(Outline.SourceOutline)
		void DefinesDecideWhatIsCompiledIn();
		TEST_FUNC(Outline.SourceOutline)
		void SyntaxErrorsAreDiagnosticsAndTheRestIsListed();
		TEST_FUNC(Outline.SourceOutline)
		void PreprocessorErrorsComeFirst();
		TEST_FUNC(Outline.SourceOutline)
		void FunctionBodiesAndIncludesDeclareNothing();
		TEST_FUNC(Outline.SourceOutline)
		void AnEmptySourceHasNoOutline();
	};
} // namespace Testing
