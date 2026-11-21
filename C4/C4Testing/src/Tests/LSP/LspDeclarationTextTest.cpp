#include "LspDeclarationTextTest.h"
#include "LspTestSupport.h"
#include "Features/Hover.h"
#include <string>

using namespace C4LSP::Features;
using Testing::Lsp::Contains;

namespace Testing
{
	namespace
	{
		const char* kRing =
			R"(#define RING_CAPACITY 64
#define TWICE(x) ((x) * 2)
class Ring
{
	public static const int32 kLimit = TWICE(RING_CAPACITY);
}
int32 first = TWICE(1); int32 second = 3;
int32 main() { return Ring::kLimit + first + second; }
)";
	}

	void LspDeclarationTextTest::AMacroUseInADeclarationReadsAsItsExpansion()
	{
		auto session = Testing::Lsp::MakeSession("Ring.cf", kRing);
		auto hover = Hover::At(session, Testing::Lsp::UriFor("Ring.cf"), Testing::Lsp::PositionOf(kRing, "kLimit +"));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Contains(hover.contents, "kLimit = ((64) * 2)"));
		TEST_PASSED();
	}

	void LspDeclarationTextTest::ADeclarationAfterAMacroUseOnItsLineIsFound()
	{
		auto session = Testing::Lsp::MakeSession("Ring.cf", kRing);
		const std::string uri = Testing::Lsp::UriFor("Ring.cf");
		auto declared = Hover::At(session, uri, Testing::Lsp::PositionOf(kRing, "second", 1)); // where the file declares it, after a use on its line
		TEST_ASSERT(declared.found);
		TEST_ASSERT(Contains(declared.contents, "int32 second = 3"));
		auto used = Hover::At(session, uri, Testing::Lsp::PositionOf(kRing, "second;", 1));
		TEST_ASSERT(used.found);
		TEST_ASSERT(Contains(used.contents, "int32 second"));
		TEST_PASSED();
	}
}
