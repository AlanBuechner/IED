#include "LspNewFeaturesTest.h"
#include <string>

using namespace C4LSP::Server;
using namespace C4LSP::Features;
using Testing::Lsp::Contains;

namespace Testing
{
	// A program exercising functions, a struct, and a call with two arguments.
	static const char* kSigFixture =
		R"(int32 Add(int32 a, int32 b) {
	return a + b;
}

void UseIt() {
	int32 total = 0;
	total = Add(total, 5);
}

int32 main() {
	UseIt();
	return 0;
}
)";

	// A program with inheritance for go-to-implementation.
	static const char* kImplFixture =
		R"(struct Shape {
	int32 id;
};

struct Circle : public Shape {
	int32 radius;
};

struct Square : public Shape {
	int32 side;
};

int32 main() {
	return 0;
}
)";

	static C4LSP::Protocol::Position BeyondFile()
	{
		C4LSP::Protocol::Position pos;
		pos.line = 9999;
		pos.character = 0;
		return pos;
	}

	void LspNewFeaturesTest::TestSignatureHelpShowsCalleeParameters()
	{
		auto session = Testing::Lsp::MakeSession("Sig.cf", kSigFixture);
		auto uri = Testing::Lsp::UriFor("Sig.cf");
		// The "Add" callee in the call (2nd occurrence; 1st is the definition).
		auto pos = Testing::Lsp::PositionOf(kSigFixture, "Add", 2);

		auto result = SignatureHelp::At(session, uri, pos);
		TEST_ASSERT(result.found);
		TEST_ASSERT_EQ(result.signatures.Count(), 1u);
		// The signature must name both parameters with their types.
		TEST_ASSERT(Contains(result.signatures[0].label, "int32 a"));
		TEST_ASSERT(Contains(result.signatures[0].label, "int32 b"));
		TEST_ASSERT_EQ(result.signatures[0].parameters.Count(), 2u);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestSignatureHelpTracksActiveArgument()
	{
		auto session = Testing::Lsp::MakeSession("Sig.cf", kSigFixture);
		auto uri = Testing::Lsp::UriFor("Sig.cf");
		// Place the caret right after the first argument "total" in "Add(total, 5)".
		auto totalPos = Testing::Lsp::PositionOf(kSigFixture, "Add(total");
		totalPos.character += 10; // past "Add(total"

		auto result = SignatureHelp::At(session, uri, totalPos);
		TEST_ASSERT(result.found);
		// Caret is between arg 0 and arg 1 -> active parameter index 1.
		TEST_ASSERT_EQ(result.activeParameter, 1u);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestSignatureHelpOnNonCallReturnsNotFound()
	{
		auto session = Testing::Lsp::MakeSession("Sig.cf", kSigFixture);
		auto uri = Testing::Lsp::UriFor("Sig.cf");

		auto result = SignatureHelp::At(session, uri, BeyondFile());
		TEST_ASSERT(!result.found);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestTypeDefinitionFromVariableDeclarationJumpsToStruct()
	{
		const char* program =
			R"(struct Point {
	int32 x;
};

void UseIt() {
	Point p;
	p.x = 1;
}
)";
		auto session = Testing::Lsp::MakeSession("TypeDef.cf", program);
		auto uri = Testing::Lsp::UriFor("TypeDef.cf");
		// The type name "Point" in the declaration "Point p;" (2nd occurrence; 1st is the struct def).
		auto pos = Testing::Lsp::PositionOf(program, "Point", 2);

		auto locations = TypeDefinition::At(session, uri, pos);
		TEST_ASSERT_EQ(locations.Count(), 1u);

		// Must land on the struct definition (first occurrence of "Point").
		auto declPos = Testing::Lsp::PositionOf(program, "Point", 1);
		TEST_ASSERT_EQ(locations[0].range.start.line, declPos.line);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestTypeDefinitionOnNonSymbolReturnsEmpty()
	{
		auto session = Testing::Lsp::MakeSession("TypeDef.cf", kSigFixture);
		auto uri = Testing::Lsp::UriFor("TypeDef.cf");

		auto locations = TypeDefinition::At(session, uri, BeyondFile());
		TEST_ASSERT_EQ(locations.Count(), 0u);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestImplementationFindsDerivedTypes()
	{
		auto session = Testing::Lsp::MakeSession("Impl.cf", kImplFixture);
		auto uri = Testing::Lsp::UriFor("Impl.cf");
		// The base type name "Shape" in its own definition (1st occurrence).
		auto pos = Testing::Lsp::PositionOf(kImplFixture, "Shape", 1);

		auto locations = Implementation::At(session, uri, pos);
		bool hasCircle = false, hasSquare = false;
		for (const auto& loc : locations)
		{
			auto circlePos = Testing::Lsp::PositionOf(kImplFixture, "Circle");
			auto squarePos = Testing::Lsp::PositionOf(kImplFixture, "Square");
			if (loc.range.start.line == circlePos.line) hasCircle = true;
			if (loc.range.start.line == squarePos.line) hasSquare = true;
		}
		TEST_ASSERT(hasCircle);
		TEST_ASSERT(hasSquare);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestImplementationOfBaseWithNoDerivativesIsEmpty()
	{
		const char* program =
			R"(struct Lone {
	int32 v;
};

int32 main() {
	return 0;
}
)";
		auto session = Testing::Lsp::MakeSession("ImplNone.cf", program);
		auto uri = Testing::Lsp::UriFor("ImplNone.cf");
		auto pos = Testing::Lsp::PositionOf(program, "Lone", 1);

		auto locations = Implementation::At(session, uri, pos);
		TEST_ASSERT_EQ(locations.Count(), 0u);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestRenameGlobalVariableCoversDeclarationAndUses()
	{
		const char* program =
			R"(int32 g_value = 0;

void UseIt() {
	g_value = 5;
}

int32 main() {
	UseIt();
	return g_value;
}
)";
		auto session = Testing::Lsp::MakeSession("Rename.cf", program);
		auto uri = Testing::Lsp::UriFor("Rename.cf");
		// Rename from the *use* of g_value (2nd occurrence).
		auto pos = Testing::Lsp::PositionOf(program, "g_value", 2);

		auto result = Rename::At(session, uri, pos);
		TEST_ASSERT(result.found);
		// Declaration + 2 uses = 3 occurrences.
		TEST_ASSERT_EQ(result.occurrences.Count(), 3u);

		// The declaration (1st occurrence) must be among the edits.
		auto declPos = Testing::Lsp::PositionOf(program, "g_value", 1);
		bool foundDecl = false;
		for (const auto& occ : result.occurrences)
			if (occ.range.start.line == declPos.line && occ.range.start.character == declPos.character)
				foundDecl = true;
		TEST_ASSERT(foundDecl);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestRenameFunctionCoversDefinitionAndCalls()
	{
		const char* program =
			R"(int32 Compute(int32 x) {
	return x * 2;
}

int32 main() {
	int32 r = Compute(10);
	return r;
}
)";
		auto session = Testing::Lsp::MakeSession("RenameFn.cf", program);
		auto uri = Testing::Lsp::UriFor("RenameFn.cf");
		// Rename from the *call* of Compute (2nd occurrence; 1st is the definition).
		auto pos = Testing::Lsp::PositionOf(program, "Compute", 2);

		auto result = Rename::At(session, uri, pos);
		TEST_ASSERT(result.found);
		// Definition + 1 call = 2 occurrences.
		TEST_ASSERT_EQ(result.occurrences.Count(), 2u);

		auto defPos = Testing::Lsp::PositionOf(program, "Compute", 1);
		bool foundDef = false;
		for (const auto& occ : result.occurrences)
			if (occ.range.start.line == defPos.line && occ.range.start.character == defPos.character)
				foundDef = true;
		TEST_ASSERT(foundDef);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestRenameOnNonSymbolReturnsNotFound()
	{
		auto session = Testing::Lsp::MakeSession("RenameNone.cf", kSigFixture);
		auto uri = Testing::Lsp::UriFor("RenameNone.cf");

		auto result = Rename::At(session, uri, BeyondFile());
		TEST_ASSERT(!result.found);
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestPrepareRenameSelectsNameTokenAtReferenceAndDeclaration()
	{
		const char* program =
			R"(int32 g_value = 0;

void UseIt() {
	g_value = 5;
}

int32 main() {
	UseIt();
	return g_value;
}
)";
		auto session = Testing::Lsp::MakeSession("PrepareRename.cf", program);
		auto uri = Testing::Lsp::UriFor("PrepareRename.cf");

		// From a *use* (reference): the selected span is that use's own name token.
		auto refPos = Testing::Lsp::PositionOf(program, "g_value", 2); // inside UseIt()
		auto atRef = Rename::PrepareAt(session, uri, refPos);
		TEST_ASSERT(atRef.found);
		TEST_ASSERT_EQ(atRef.placeholder, std::string("g_value"));

		// The range must cover exactly the token (7 chars) on that line.
		size_t spanLen = atRef.range.end.character - atRef.range.start.character;
		TEST_ASSERT_EQ(spanLen, static_cast<size_t>(atRef.placeholder.size()));
		TEST_ASSERT_EQ(atRef.range.start.line, refPos.line);

		// From the *declaration* itself: same token shape.
		auto declPos = Testing::Lsp::PositionOf(program, "g_value", 1);
		auto atDecl = Rename::PrepareAt(session, uri, declPos);
		TEST_ASSERT(atDecl.found);
		TEST_ASSERT_EQ(atDecl.placeholder, std::string("g_value"));

		// A function name is equally rename-able from a call site.
		const char* fnProgram =
			R"(int32 Compute(int32 x) {
	return x * 2;
}

int32 main() {
	int32 r = Compute(10);
	return r;
}
)";
		auto fnSession = Testing::Lsp::MakeSession("PrepareRenameFn.cf", fnProgram);
		auto fnUri = Testing::Lsp::UriFor("PrepareRenameFn.cf");
		auto callPos = Testing::Lsp::PositionOf(fnProgram, "Compute(10"); // the call site
		auto atCall = Rename::PrepareAt(fnSession, fnUri, callPos);
		TEST_ASSERT(atCall.found);
		TEST_ASSERT_EQ(atCall.placeholder, std::string("Compute"));
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestPrepareRenameOnNonSymbolReturnsNotFound()
	{
		auto session = Testing::Lsp::MakeSession("PrepareNone.cf", kSigFixture);
		auto uri = Testing::Lsp::UriFor("PrepareNone.cf");

		auto result = Rename::PrepareAt(session, uri, BeyondFile());
		TEST_ASSERT(!result.found);
		TEST_PASSED();
	}

	static const char* kIncludeMain =
		R"(include "math.ch"

int32 main() {
	return Add(1, 2);
}
)";

	void LspNewFeaturesTest::TestIncludeDefinitionNavigatesToIncludedFile()
	{
		C4LSP::Server::CompilationSession session;
		session.RegisterInMemorySource("math.ch", "int32 Add(int32 a, int32 b) { return a + b; }\n");
		session.RegisterInMemorySource("main.cf", kIncludeMain);
		session.Recompile();

		auto uri = Testing::Lsp::UriFor("main.cf");
		// Caret inside the include path string ("math.ch").
		auto pos = Testing::Lsp::PositionOf(kIncludeMain, "math.ch");

		auto result = Definition::At(session, uri, pos);
		TEST_ASSERT_EQ(result.Count(), 1u);
		TEST_ASSERT(Testing::Lsp::Contains(result[0].uri, "math.ch"));
		TEST_ASSERT_EQ(result[0].range.start.line, 0u);

		// Caret on the `include` keyword itself navigates to the same file.
		auto kwPos = Testing::Lsp::PositionOf(kIncludeMain, "include");
		auto byKeyword = Definition::At(session, uri, kwPos);
		TEST_ASSERT_EQ(byKeyword.Count(), 1u);
		TEST_ASSERT(Testing::Lsp::Contains(byKeyword[0].uri, "math.ch"));
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestIncludeDefinitionUnresolvablePathReturnsEmpty()
	{
		C4LSP::Server::CompilationSession session;
		session.RegisterInMemorySource("main.cf", "include \"missing.ch\"\nint32 main() { return 0; }\n");
		session.Recompile();

		auto uri = Testing::Lsp::UriFor("main.cf");
		auto pos = Testing::Lsp::PositionOf("include \"missing.ch\"", "missing.ch");

		auto result = Definition::At(session, uri, pos);
		TEST_ASSERT_EQ(result.Count(), 0u); // unresolvable include must not crash or misjump
		TEST_PASSED();
	}

	void LspNewFeaturesTest::TestSymbolDefinitionStillWorksBesideIncludes()
	{
		C4LSP::Server::CompilationSession session;
		session.RegisterInMemorySource("math.ch", "int32 Add(int32 a, int32 b) { return a + b; }\n");
		session.RegisterInMemorySource("main.cf", kIncludeMain);
		session.Recompile();

		auto uri = Testing::Lsp::UriFor("main.cf");
		// Caret on the Add() call (not inside any include): normal symbol resolution.
		auto pos = Testing::Lsp::PositionOf(kIncludeMain, "Add(1, 2)");

		auto result = Definition::At(session, uri, pos);
		TEST_ASSERT_EQ(result.Count(), 1u);
		TEST_ASSERT(Testing::Lsp::Contains(result[0].uri, "math.ch")); // lands on the definition line in math.ch
		TEST_PASSED();
	}
} // namespace Testing
