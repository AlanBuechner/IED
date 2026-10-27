#include "LspScopeNavigationTest.h"
#include "LspTestSupport.h"
#include "Features/Definition.h"
#include "Features/Hover.h"
#include "Features/References.h"
#include "Features/DocumentHighlight.h"
#include "Features/Rename.h"
#include "Features/SemanticTokens.h"
#include "Features/Tidy.h"

using namespace C4LSP::Features;
using Testing::Lsp::Contains;
using Testing::Lsp::PositionOf;

namespace Testing
{
	namespace
	{
		// "left" is a lambda's parameter (1) and its use (2); "rank" the field (1) and its uses (2..6); "Better" a function
		// (1) called in a lambda (2); "counter" a local (1) a lambda captures (2) and uses (3).
		const char* const kLambdas =
			R"(struct Entry
{
	public int32 rank;
	public int32 Twice() { return rank * 2; }
}

bool Better(const Entry& a, const Entry& b) { return a.rank > b.rank; }

void Sort(func<bool(const Entry&, const Entry&)> before) { }

int32 main()
{
	Sort([](const Entry& a, const Entry& b) -> bool { return Better(a, b); });
	Sort([](const Entry& left, const Entry& right) -> bool { return left.rank < right.rank; });
	Entry counter;
	func<int32()> twice = [&counter]() -> int32 { return counter.Twice(); };
	return twice();
}
)";

		// "next" is an init capture (1) its body uses (2, 3); "base" the local its initializer reads (1, 2).
		const char* const kInitCapture =
			R"(int32 main()
{
	int32 base = 2;
	func<int32()> plus = [next = base + 1]() -> int32 { return next * next; };
	return plus();
}
)";

		// A lambda's local named like a field elsewhere: "rank" is the field (1), the lambda's local (2) and its use (3).
		const char* const kColors =
			R"(struct Entry { public int32 rank; }

int32 main()
{
	func<int32()> local = []() -> int32 { int32 rank = 3; return rank; };
	return local();
}
)";

		const char* const kLoop =
			R"(int32 Sum(int32[4] numbers)
{
	int32 total = 0;
	for (const int32 item : numbers)
		total += item;
	return total;
}

int32 main() { int32[4] four = { 1, 2, 3, 4 }; return Sum(four); }
)";

		const char* const kHeader =
			R"(class Counter
{
	public int32 value;
	public int32 Next();
}
)";

		const char* const kCode =
			R"(include "counter.ch"

int32 Counter::Next() {
	value = value + 1;
	return value;
}
)";

		const char* const kMain =
			R"(include "counter.ch"

int32 main() {
	Counter c;
	return c.Next();
}
)";

		bool Same(const C4LSP::Protocol::Position& a, const C4LSP::Protocol::Position& b)
		{
			return a.line == b.line && a.character == b.character;
		}

		bool LandsOn(const Core::Vector<C4LSP::Protocol::Location>& locations, const std::string& file, const C4LSP::Protocol::Position& at)
		{
			return locations.Count() == 1 && Contains(locations[0].uri, file) && Same(locations[0].range.start, at);
		}

		Core::Vector<C4LSP::Protocol::Location> DefinitionAt(C4LSP::Server::CompilationSession& session, const char* program, const std::string& file,
			const std::string& needle, int occurrence = 1)
		{
			return Definition::At(session, Lsp::UriFor(file), PositionOf(program, needle, occurrence));
		}

		std::string HoverAt(C4LSP::Server::CompilationSession& session, const char* program, const std::string& file, const std::string& needle, int occurrence = 1)
		{
			return Hover::At(session, Lsp::UriFor(file), PositionOf(program, needle, occurrence)).contents;
		}
	}

	void LspScopeNavigationTest::TestLambdaParametersAndBodyResolve()
	{
		auto session = Lsp::MakeSession("Lambdas.cf", kLambdas);
		const std::string uri = Lsp::UriFor("Lambdas.cf");
		TEST_ASSERT(LandsOn(DefinitionAt(session, kLambdas, "Lambdas.cf", "left", 1), "Lambdas.cf", PositionOf(kLambdas, "left", 1)));
		TEST_ASSERT(LandsOn(DefinitionAt(session, kLambdas, "Lambdas.cf", "left", 2), "Lambdas.cf", PositionOf(kLambdas, "left", 1)));
		TEST_ASSERT(LandsOn(DefinitionAt(session, kLambdas, "Lambdas.cf", "rank", 5), "Lambdas.cf", PositionOf(kLambdas, "rank", 1)));
		TEST_ASSERT(LandsOn(DefinitionAt(session, kLambdas, "Lambdas.cf", "a, b); }"), "Lambdas.cf", PositionOf(kLambdas, "a, const Entry& b) -> bool")));
		const std::string hover = HoverAt(session, kLambdas, "Lambdas.cf", "left", 2);
		TEST_ASSERT(Contains(hover, "parameter `left`"));
		TEST_ASSERT(Contains(hover, "const Entry& left"));
		TEST_ASSERT(Contains(HoverAt(session, kLambdas, "Lambdas.cf", "rank", 5), "int32 Entry::rank"));
		// Code inside a lambda never names the call the lambda is an argument of, nor the function around it.
		TEST_ASSERT(!Hover::At(session, uri, PositionOf(kLambdas, "return left")).found);
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kLambdas, "left", 1), true).Count(), 2u);
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, PositionOf(kLambdas, "left", 2)).Count(), 2u);
		TEST_ASSERT_EQ(Rename::At(session, uri, PositionOf(kLambdas, "left", 2)).occurrences.Count(), 2u);
		TEST_PASSED();
	}

	void LspScopeNavigationTest::TestCallsInALambdaAreReferences()
	{
		auto session = Lsp::MakeSession("Lambdas.cf", kLambdas);
		const std::string uri = Lsp::UriFor("Lambdas.cf");
		TEST_ASSERT(LandsOn(DefinitionAt(session, kLambdas, "Lambdas.cf", "Better", 2), "Lambdas.cf", PositionOf(kLambdas, "Better", 1)));
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kLambdas, "Better", 1), true).Count(), 2u);
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kLambdas, "rank", 1), true).Count(), 6u);
		TEST_PASSED();
	}

	void LspScopeNavigationTest::TestLambdaCapturesNameTheirVariable()
	{
		auto session = Lsp::MakeSession("Lambdas.cf", kLambdas);
		const std::string uri = Lsp::UriFor("Lambdas.cf");
		const auto local = PositionOf(kLambdas, "counter", 1);
		TEST_ASSERT(LandsOn(DefinitionAt(session, kLambdas, "Lambdas.cf", "counter", 2), "Lambdas.cf", local)); // the capture
		TEST_ASSERT(LandsOn(DefinitionAt(session, kLambdas, "Lambdas.cf", "counter", 3), "Lambdas.cf", local)); // the body's use
		TEST_ASSERT(LandsOn(DefinitionAt(session, kLambdas, "Lambdas.cf", "Twice", 2), "Lambdas.cf", PositionOf(kLambdas, "Twice", 1)));
		TEST_ASSERT(Contains(HoverAt(session, kLambdas, "Lambdas.cf", "counter", 2), "local `counter`"));
		TEST_ASSERT_EQ(References::At(session, uri, local, true).Count(), 3u);
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, PositionOf(kLambdas, "counter", 3)).Count(), 3u);
		TEST_PASSED();
	}

	void LspScopeNavigationTest::TestRenameReachesLambdaCaptures()
	{
		auto session = Lsp::MakeSession("Lambdas.cf", kLambdas);
		const std::string uri = Lsp::UriFor("Lambdas.cf");
		auto renamed = Rename::At(session, uri, PositionOf(kLambdas, "counter", 1));
		TEST_ASSERT_EQ(renamed.occurrences.Count(), 3u);
		bool captureRenamed = false;
		for (const auto& occurrence : renamed.occurrences)
			captureRenamed = captureRenamed || Same(occurrence.range.start, PositionOf(kLambdas, "counter", 2));
		TEST_ASSERT(captureRenamed);
		auto prepared = Rename::PrepareAt(session, uri, PositionOf(kLambdas, "counter", 2));
		TEST_ASSERT(prepared.found);
		TEST_ASSERT_EQ(prepared.placeholder, std::string("counter"));
		TEST_ASSERT(Same(prepared.range.start, PositionOf(kLambdas, "counter", 2)));
		TEST_PASSED();
	}

	void LspScopeNavigationTest::TestInitCapturesDeclareAVariable()
	{
		auto session = Lsp::MakeSession("Init.cf", kInitCapture);
		const std::string uri = Lsp::UriFor("Init.cf");
		TEST_ASSERT(LandsOn(DefinitionAt(session, kInitCapture, "Init.cf", "next", 2), "Init.cf", PositionOf(kInitCapture, "next", 1)));
		TEST_ASSERT(LandsOn(DefinitionAt(session, kInitCapture, "Init.cf", "base", 2), "Init.cf", PositionOf(kInitCapture, "base", 1)));
		const std::string hover = HoverAt(session, kInitCapture, "Init.cf", "next", 3);
		TEST_ASSERT(Contains(hover, "local `next`"));
		TEST_ASSERT(Contains(hover, "next = base + 1"));
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kInitCapture, "next", 1), true).Count(), 3u);
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, PositionOf(kInitCapture, "next", 2)).Count(), 3u);
		TEST_ASSERT_EQ(Rename::At(session, uri, PositionOf(kInitCapture, "next", 2)).occurrences.Count(), 3u);
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kInitCapture, "base", 1), true).Count(), 2u);
		TEST_PASSED();
	}

	void LspScopeNavigationTest::TestALocalUsedOnlyInALambdaIsUsed()
	{
		const std::string program =
			"int32 g_out;\nvoid Run()\n{\n\tint32 kept = 5;\n\tfunc<void()> use = [&kept]() -> void { g_out = kept; };\n\tuse();\n"
			"\tfunc<void()> other = []() -> void { int32 idle; };\n\tother();\n}\n";
		Tidy::SetEnabled(true);
		auto session = Lsp::MakeSession("tidy_lambda.cf", program);
		auto diagnostics = Tidy::ForDocument(session, Lsp::UriFor("tidy_lambda.cf"));
		bool keptReported = false;
		bool idleReported = false;
		for (const auto& diagnostic : diagnostics)
		{
			keptReported = keptReported || Contains(diagnostic.message, "unused local variable 'kept'");
			idleReported = idleReported || Contains(diagnostic.message, "unused local variable 'idle'");
		}
		TEST_ASSERT(!keptReported);
		TEST_ASSERT(idleReported); // a lambda's own locals are checked too
		TEST_PASSED();
	}

	void LspScopeNavigationTest::TestLambdaBodiesAreColored()
	{
		auto session = Lsp::MakeSession("Colors.cf", kColors);
		auto result = SemanticTokens::ForDocument(session, Lsp::UriFor("Colors.cf"));
		for (int occurrence : { 2, 3 }) // the lambda's local and its use: a variable, not the field named alike
		{
			const auto at = PositionOf(kColors, "rank", occurrence);
			bool variable = false;
			for (const auto& token : result.tokens)
				if (token.startLine == at.line && token.startCharacter == at.character)
					variable = token.kindId == static_cast<uint32>(SemanticTokenKind::Variable);
			TEST_ASSERT(variable);
		}
		TEST_PASSED();
	}

	void LspScopeNavigationTest::TestRangeForVariableIsItsName()
	{
		auto session = Lsp::MakeSession("Loop.cf", kLoop);
		const std::string uri = Lsp::UriFor("Loop.cf");
		const auto declared = PositionOf(kLoop, "item", 1);
		auto definition = DefinitionAt(session, kLoop, "Loop.cf", "item", 2);
		TEST_ASSERT(LandsOn(definition, "Loop.cf", declared));
		TEST_ASSERT_EQ(definition[0].range.end.line, declared.line);
		TEST_ASSERT_EQ(definition[0].range.end.character, declared.character + 4);
		auto references = References::At(session, uri, PositionOf(kLoop, "item", 2), true);
		TEST_ASSERT_EQ(references.Count(), 2u);
		TEST_ASSERT(Same(references[0].range.start, declared) && references[0].range.end.character == declared.character + 4);
		const std::string hover = HoverAt(session, kLoop, "Loop.cf", "item", 2);
		TEST_ASSERT(Contains(hover, "local `item`"));
		TEST_ASSERT(Contains(hover, "const int32 item"));
		TEST_ASSERT(Contains(HoverAt(session, kLoop, "Loop.cf", "item", 1), "local `item`"));
		auto renamed = Rename::At(session, uri, PositionOf(kLoop, "item", 2));
		TEST_ASSERT_EQ(renamed.occurrences.Count(), 2u);
		bool declarationRenamed = false;
		for (const auto& occurrence : renamed.occurrences)
			declarationRenamed = declarationRenamed || (Same(occurrence.range.start, declared) && occurrence.range.end.character == declared.character + 4);
		TEST_ASSERT(declarationRenamed);
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, declared).Count(), 2u);
		TEST_PASSED();
	}

	void LspScopeNavigationTest::TestDefinitionFromADeclarationsOwnName()
	{
		C4LSP::Server::CompilationSession session;
		session.RegisterInMemorySource("counter.ch", kHeader);
		session.RegisterInMemorySource("counter.cf", kCode);
		session.RegisterInMemorySource("main.cf", kMain);
		session.Recompile();
		TEST_ASSERT(LandsOn(DefinitionAt(session, kCode, "counter.cf", "Next"), "counter.ch", PositionOf(kHeader, "Next")));       // its declaration
		TEST_ASSERT(LandsOn(DefinitionAt(session, kCode, "counter.cf", "Counter"), "counter.ch", PositionOf(kHeader, "Counter"))); // its type
		TEST_ASSERT(LandsOn(DefinitionAt(session, kHeader, "counter.ch", "Next"), "counter.cf", PositionOf(kCode, "Next")));       // its body
		TEST_ASSERT(LandsOn(DefinitionAt(session, kMain, "main.cf", "c;"), "main.cf", PositionOf(kMain, "c;")));                 // itself
		TEST_ASSERT(Contains(HoverAt(session, kCode, "counter.cf", "Counter"), "class Counter"));
		TEST_PASSED();
	}
} // namespace Testing
