#include "IdentifierTokenizationTest.h"

namespace Testing
{
	// Consolidated: structural, control flow, built-in type, and visibility keywords in one pass
	void IdentifierTokenizationTest::TestAllKeywords()
	{
		auto stream = Scripting::Source::FromMemory(
			"namespace class struct enum "
			"if else for while return break continue "
			"true false null this "
			"public private protected");
		Scripting::Lexer lexer(*stream, 0);

		// Structural keywords
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Namespace);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Class);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Struct);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Enum);

		// Control flow keywords
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::If);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Else);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::For);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::While);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Return);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Break);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Continue);

		// Built-in type and literal keywords
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::True);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::False);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Null);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::This);

		// Visibility modifiers
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Public);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Private);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Protected);

		TEST_PASSED();
	}

	void IdentifierTokenizationTest::TestNonKeywordRemainsIdentifier()
	{
		auto stream = Scripting::Source::FromMemory("myVar");
		Scripting::Lexer lexer(*stream, 0);

		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == Scripting::TokenType::Identifier);
		TEST_ASSERT(t.GetText() == "myVar");
		TEST_PASSED();
	}

	void IdentifierTokenizationTest::TestCaseSensitivity()
	{
		// "If" (capital I) should not promote to If keyword
		auto stream = Scripting::Source::FromMemory("If TRUE");
		Scripting::Lexer lexer(*stream, 0);

		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Identifier);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}

	void IdentifierTokenizationTest::TestEveryKeywordPromotes()
	{
		using T = Scripting::TokenType;
		const struct { const char* text; T type; } keywords[] = {
			{ "namespace", T::Namespace }, { "class", T::Class }, { "struct", T::Struct }, { "interface", T::Interface },
			{ "enum", T::Enum }, { "union", T::Union }, { "public", T::Public }, { "private", T::Private },
			{ "protected", T::Protected }, { "virtual", T::Virtual }, { "override", T::Override }, { "const", T::Const },
			{ "static", T::Static }, { "if", T::If }, { "else", T::Else }, { "for", T::For }, { "while", T::While },
			{ "do", T::Do }, { "switch", T::Switch }, { "case", T::Case }, { "default", T::Default },
			{ "break", T::Break }, { "continue", T::Continue }, { "return", T::Return }, { "defer", T::Defer },
			{ "goto", T::Goto }, { "fallthrough", T::Fallthrough }, { "try", T::Try }, { "catch", T::Catch },
			{ "finally", T::Finally }, { "throw", T::Throw }, { "delete", T::Delete }, { "new", T::New },
			{ "true", T::True }, { "false", T::False }, { "null", T::Null }, { "nullptr", T::Null }, { "this", T::This },
			{ "funcptr", T::FunPtr }, { "alias", T::Alias }, { "template", T::Template }, { "extern", T::Extern },
			{ "is", T::Is }, { "as", T::As }, { "extends", T::Extends },
		};
		for (const auto& keyword : keywords)
		{
			auto stream = Scripting::Source::FromMemory(keyword.text);
			Scripting::Lexer lexer(*stream, 0);
			const Scripting::Token& token = lexer.NextCompoundToken();
			TEST_ASSERT(token.GetType() == keyword.type);
			TEST_ASSERT(token.GetText() == keyword.text);
		}
		TEST_PASSED();
	}

	void IdentifierTokenizationTest::TestTypeNamesAndLoopStayIdentifiers()
	{
		// D-04: `loop` is an ordinary name; type names (built-in aliases included) are never keywords.
		auto stream = Scripting::Source::FromMemory("loop int uint char char8 int32 float double bool void Loop loops");
		Scripting::Lexer lexer(*stream, 0);
		for (int i = 0; i < 12; ++i)
			TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::Identifier);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}
}
