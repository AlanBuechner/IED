#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) AnnotationTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::AnnotationTest);

		// Basic parsing
		TEST_FUNC(Parser.Annotation)
		void TestSimpleAnnotation();

		TEST_FUNC(Parser.Annotation)
		void TestAnnotationWithStringArg();

		// Multiple annotations in one bracket group
		TEST_FUNC(Parser.Annotation)
		void TestMultipleAnnotationsInOneBracket();

		// Consecutive bracket groups
		TEST_FUNC(Parser.Annotation)
		void TestConsecutiveBrackets();

		// Complex example from spec
		TEST_FUNC(Parser.Annotation)
		void TestComplexExample();

		// Qualified name in annotation
		TEST_FUNC(Parser.Annotation)
		void TestQualifiedNameAnnotation();

		// Negative: no bracket at start
		TEST_FUNC(Parser.Annotation)
		void TestNoBracketReturnsNullptr();

		// Empty brackets - error recorded, partial node returned
		TEST_FUNC(Parser.Annotation)
		void TestEmptyBrackets();

		// Arguments are expressions (D-42), the compiler's annotations take string literals
		TEST_FUNC(Parser.Annotation)
		void TestArgumentsAreExpressions();

		TEST_FUNC(Parser.Annotation)
		void TestEmptyArgumentList();

		TEST_FUNC(Parser.Annotation)
		void TestMissingArgumentIsAnError();

		TEST_FUNC(Parser.Annotation)
		void TestCompilerAnnotationTakesLiterals();

		TEST_FUNC(Parser.Annotation)
		void TestCompilerAnnotationNames();

		// BUG-158: an enum or union keeps the annotations written before it
		TEST_FUNC(Parser.Annotation)
		void TestEnumKeepsAnnotations();

		TEST_FUNC(Parser.Annotation)
		void TestUnionKeepsAnnotations();
	};
}
