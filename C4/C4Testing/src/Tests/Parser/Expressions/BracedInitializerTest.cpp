#include "BracedInitializerTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h"
#include <memory>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// The lexer, error tracker and source outlive the state that refers to them.
		struct Parsed
		{
			Ref<Source> stream;
			Lexer lexer;
			ErrorTracker errors;
			std::unique_ptr<ParserState> state;
			Ref<AstNode> node;

			explicit Parsed(const std::string& source)
				: stream(Source::FromMemory(source)), lexer(*stream, 0),
				  state(std::make_unique<ParserState>(lexer, errors)), node(PrattParser::ParseExpression(*state)) {}

			const BracedInitializerNode* Brace() const { return dynamic_cast<const BracedInitializerNode*>(node.Get()); }
		};
	}

	void BracedInitializerTest::PrintsPositionalEntries()
	{
		Parsed p("{1,2,3}");
		TEST_ASSERT(p.Brace() && p.Brace()->ToString() == "{ 1, 2, 3 }");
		Parsed trailing("{ 1, }");
		TEST_ASSERT(trailing.Brace() && trailing.Brace()->ToString() == "{ 1 }");
	}

	void BracedInitializerTest::PrintsDesignators()
	{
		Parsed p("{ 1, [5] = 9, [6...8] = 0 }");
		TEST_ASSERT(p.Brace() && p.Brace()->ToString() == "{ 1, [5] = 9, [6...8] = 0 }");
	}

	void BracedInitializerTest::PrintsAnEmptyList()
	{
		Parsed p("{}");
		TEST_ASSERT(p.Brace() && p.Brace()->ToString() == "{}");
	}

	void BracedInitializerTest::KeepsBracketedAndFieldKeysApart()
	{
		Parsed p("{ y = 2, [0] = 1 }");
		const BracedInitializerNode* brace = p.Brace();
		TEST_ASSERT(brace && brace->GetEntries().Count() == 2);
		TEST_ASSERT(!brace->GetEntries()[0].m_IsBracketed && brace->GetEntries()[1].m_IsBracketed);
		TEST_ASSERT(brace->ToString() == "{ y = 2, [0] = 1 }");
	}
} // namespace Testing
