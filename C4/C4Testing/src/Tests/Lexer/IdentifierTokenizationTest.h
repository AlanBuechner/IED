#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"

namespace Testing
{
	CLASS(GROUP = Testing) IdentifierTokenizationTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::IdentifierTokenizationTest);

		// Consolidated: all keyword categories tested in one pass
		TEST_FUNC(Lexer.Identifiers.AllKeywords)
		void TestAllKeywords();

		// Non-keyword identifiers pass through as Identifier type
		TEST_FUNC(Lexer.Identifiers.NonKeyword)
		void TestNonKeywordRemainsIdentifier();

		// Case sensitivity: "If" != "if", "TRUE" != "true"
		TEST_FUNC(Lexer.Identifiers.CaseSensitive)
		void TestCaseSensitivity();

		TEST_FUNC(Lexer.Identifiers.AllKeywords)
		void TestEveryKeywordPromotes();

		TEST_FUNC(Lexer.Identifiers.NonKeyword)
		void TestTypeNamesAndLoopStayIdentifiers();
	};
}
