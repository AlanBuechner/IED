#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The directive processor (B4, BUG-124, BUG-126, BUG-127): comments that span lines hide directives,
	// `#define F (x)` is object-like, calls and directives continue across lines, every directive and its
	// errors, #if groups, and that the output keeps every line where it was.
	CLASS(GROUP = Testing) MacroPreprocessorTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MacroPreprocessorTest);

		TEST_FUNC(Macro.Preprocessor.Comments)
		void CommentsHideDirectives();
		TEST_FUNC(Macro.Preprocessor.Define)
		void ObjectAndFunctionLikeDefinitions();
		TEST_FUNC(Macro.Preprocessor.Define)
		void DefinitionErrors();
		TEST_FUNC(Macro.Preprocessor.Lines)
		void ContinuationsAndCallsAcrossLines();
		TEST_FUNC(Macro.Preprocessor.Lines)
		void EveryLineStaysInPlace();
		TEST_FUNC(Macro.Preprocessor.Conditions)
		void ConditionalGroups();
		TEST_FUNC(Macro.Preprocessor.Conditions)
		void MisplacedConditionalsAreErrors();
		TEST_FUNC(Macro.Preprocessor.Directives)
		void OtherDirectives();
		TEST_FUNC(Macro.Preprocessor.Includes)
		void IncludesInheritMacros();
	};
} // namespace Testing
