#include "DapExpressionTest.h"
#include "Target/Expression.h"

using namespace C4DAP::Target;

namespace Testing
{
	void DapExpressionTest::AVariableAlone()
	{
		std::string error;
		const std::optional<Expression> parsed = ParseExpression("  total ", error);
		TEST_ASSERT(parsed.has_value());
		TEST_ASSERT_EQ(parsed->variable, std::string("total"));
		TEST_ASSERT(parsed->path.empty());
		TEST_ASSERT_EQ(parsed->dereferences, 0u);
		TEST_PASSED();
	}

	void DapExpressionTest::ScopedNamesStayWhole()
	{
		std::string error;
		const std::optional<Expression> parsed = ParseExpression("Tally::Inner::g_count.field", error);
		TEST_ASSERT(parsed.has_value());
		TEST_ASSERT_EQ(parsed->variable, std::string("Tally::Inner::g_count"));
		TEST_ASSERT_EQ(parsed->path.size(), size_t(1));
		TEST_ASSERT_EQ(parsed->path[0].field, std::string("field"));
		TEST_PASSED();
	}

	void DapExpressionTest::FieldsPointersAndIndexesChain()
	{
		std::string error;
		const std::optional<Expression> parsed = ParseExpression("shapes[2]->corner.x [ 10 ]", error);
		TEST_ASSERT(parsed.has_value());
		TEST_ASSERT_EQ(parsed->variable, std::string("shapes"));
		TEST_ASSERT_EQ(parsed->path.size(), size_t(4));
		TEST_ASSERT(parsed->path[0].kind == PathStep::Kind::Index);
		TEST_ASSERT_EQ(parsed->path[0].index, uint64_t(2));
		TEST_ASSERT(parsed->path[1].kind == PathStep::Kind::PointerField);
		TEST_ASSERT_EQ(parsed->path[1].field, std::string("corner"));
		TEST_ASSERT(parsed->path[2].kind == PathStep::Kind::Field);
		TEST_ASSERT_EQ(parsed->path[2].field, std::string("x"));
		TEST_ASSERT_EQ(parsed->path[3].index, uint64_t(10));
		TEST_PASSED();
	}

	void DapExpressionTest::LeadingStarsDereference()
	{
		std::string error;
		const std::optional<Expression> parsed = ParseExpression("**pp", error);
		TEST_ASSERT(parsed.has_value());
		TEST_ASSERT_EQ(parsed->dereferences, 2u);
		TEST_ASSERT_EQ(parsed->variable, std::string("pp"));
		TEST_PASSED();
	}

	void DapExpressionTest::MalformedTextIsAnError()
	{
		for (const char* text : { "", "   ", "3", "*", "a.", "a->", "a[", "a[x]", "a[1", "a b", "a+b", "a::", "a[99999999999999999999]" })
		{
			std::string error;
			const std::optional<Expression> parsed = ParseExpression(text, error);
			TEST_ASSERT(!parsed.has_value());
			TEST_ASSERT(!error.empty());
		}
		TEST_PASSED();
	}
} // namespace Testing
