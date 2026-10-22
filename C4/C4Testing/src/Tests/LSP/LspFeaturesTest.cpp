#include "LspFeaturesTest.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace C4LSP::Server;
using namespace C4LSP::Features;
using Testing::Lsp::Contains;

namespace Testing
{
	// A single, self-contained C4 program used as the fixture for most feature tests. It exercises
	// globals, a struct with fields, an enum, functions, member access and calls so that hover,
	// definition, completion and symbols all have real targets to resolve.
	static const char* kFixture =
		R"(int32 g_counter = 0;

struct Point {
	int32 x;
	int32 y;
};

enum Color { Red, Green, Blue };

int32 Add(int32 a, int32 b) {
	return a + b;
}

void UseIt() {
	int32 total = g_counter;
	Point p;
	p.x = 5;
	total = Add(total, p.x);
	Color c = Green;
}

int32 main() {
	UseIt();
	return 0;
}
)";

	// The start-of-line position (column 0) of the line containing `needle`, so completion is
	// computed with an empty typed prefix and returns the full candidate set.
	static C4LSP::Protocol::Position StartOfLine(const std::string& text, const std::string& needle)
	{
		C4LSP::Protocol::Position pos = Testing::Lsp::PositionOf(text, needle);
		pos.character = 0;
		return pos;
	}

	// A position far past the end of the file: guaranteed to sit on no symbol.
	static C4LSP::Protocol::Position BeyondFile()
	{
		C4LSP::Protocol::Position pos;
		pos.line = 9999;
		pos.character = 0;
		return pos;
	}

	void LspFeaturesTest::TestHoverOnGlobalVariableShowsType()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// The *use* of g_counter (2nd occurrence), not its declaration.
		auto pos = Testing::Lsp::PositionOf(kFixture, "g_counter", 2);

		auto result = Hover::At(session, uri, pos);
		TEST_ASSERT(result.found);
		TEST_ASSERT(Contains(result.contents, "int32"));
		TEST_ASSERT(Contains(result.contents, "g_counter"));
		TEST_PASSED();
	}

	void LspFeaturesTest::TestHoverOnFunctionCallCalleeShowsSignature()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// The callee "Add" in the call (2nd occurrence; 1st is the definition).
		auto pos = Testing::Lsp::PositionOf(kFixture, "Add", 2);

		auto result = Hover::At(session, uri, pos);
		TEST_ASSERT(result.found);
		TEST_ASSERT(Contains(result.contents, "Add"));
		TEST_ASSERT(Contains(result.contents, "int32"));
		TEST_PASSED();
	}

	void LspFeaturesTest::TestHoverOnClassFieldShowsFieldType()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// The member name in "p.x = 5" (2nd occurrence of "x"; 1st is the field declaration).
		auto pos = Testing::Lsp::PositionOf(kFixture, "x", 2);

		auto result = Hover::At(session, uri, pos);
		TEST_ASSERT(result.found);
		TEST_ASSERT(Contains(result.contents, "int32"));
		TEST_PASSED();
	}

	void LspFeaturesTest::TestHoverOnLocalVariableShowsType()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// The local "total" used as the assignment target (2nd occurrence; 1st is its declaration).
		auto pos = Testing::Lsp::PositionOf(kFixture, "total", 2);

		auto result = Hover::At(session, uri, pos);
		TEST_ASSERT(result.found);
		TEST_ASSERT(Contains(result.contents, "int32"));
		TEST_PASSED();
	}

	void LspFeaturesTest::TestHoverWithNoSymbolReturnsNotFound()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");

		auto result = Hover::At(session, uri, BeyondFile());
		TEST_ASSERT(!result.found);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestHoverOnAParameterShowsItsTypeOrItself()
	{
		const std::string program =
			R"(struct Span { int32 n; };
int32 Length(Span span) { return span.n; }
int32 main() { return 0; }
)";
		auto session = Testing::Lsp::MakeSession("ParamHover.cf", program);
		auto uri = Testing::Lsp::UriFor("ParamHover.cf");

		auto type = Hover::At(session, uri, Testing::Lsp::PositionOf(program, "Span", 2)); // the parameter's type
		TEST_ASSERT(type.found);
		TEST_ASSERT(Contains(type.contents, "struct Span"));
		TEST_ASSERT(!Contains(type.contents, "function"));

		auto name = Hover::At(session, uri, Testing::Lsp::PositionOf(program, "span", 1)); // the parameter's name
		TEST_ASSERT(name.found);
		TEST_ASSERT(Contains(name.contents, "parameter Span span"));
		TEST_PASSED();
	}

	void LspFeaturesTest::TestHoverInsideAMethodWithParametersResolvesItsMembers()
	{
		const std::string program =
			R"(namespace Shapes {
	struct Size { int32 extent; };
	class Box {
		public int32 Width() const { return 2; }
		public int32 Area(int32 scale) const { return Width() * scale; }
		public int32 Grow(const Size& base) const { return base.extent; }
		public Size Corner() const { Size s; return s; }
		public int32 Wide() const { return Corner().extent; }
	};
}
int32 main() { return 0; }
)";
		auto session = Testing::Lsp::MakeSession("Members.cf", program);
		auto uri = Testing::Lsp::UriFor("Members.cf");

		// BUG-337: `Width()` inside Area(int32 scale) is the method, not the function around it.
		auto call = Hover::At(session, uri, Testing::Lsp::PositionOf(program, "Width", 2));
		TEST_ASSERT(call.found);
		TEST_ASSERT(Contains(call.contents, "Width"));
		TEST_ASSERT(!Contains(call.contents, "Area"));

		// BUG-337: `base` (a `const Size&` in namespace Shapes) has its type, so `base.extent` resolves.
		auto field = Hover::At(session, uri, Testing::Lsp::PositionOf(program, "extent", 2));
		TEST_ASSERT(field.found);
		TEST_ASSERT(Contains(field.contents, "int32 extent"));

		// BUG-338: a member of a call's result, `Corner().extent`.
		auto ofCall = Hover::At(session, uri, Testing::Lsp::PositionOf(program, "extent", 3));
		TEST_ASSERT(ofCall.found);
		TEST_ASSERT(Contains(ofCall.contents, "int32 extent"));
		TEST_PASSED();
	}

	void LspFeaturesTest::TestHoverOnAConstructorCallShowsItsType()
	{
		const std::string program =
			R"(struct Span {
	[operator("create")] void Init(int32 count) { extent = count; }
	int32 extent;
};
int32 Length(int32 count) { return Span(count).extent; }
int32 main() { return 0; }
)";
		auto session = Testing::Lsp::MakeSession("CtorHover.cf", program);
		auto uri = Testing::Lsp::UriFor("CtorHover.cf");

		auto type = Hover::At(session, uri, Testing::Lsp::PositionOf(program, "Span", 2)); // `Span(count)`
		TEST_ASSERT(type.found);
		TEST_ASSERT(Contains(type.contents, "struct Span"));

		auto member = Hover::At(session, uri, Testing::Lsp::PositionOf(program, "extent", 3)); // `.extent` of the constructed value
		TEST_ASSERT(member.found);
		TEST_ASSERT(Contains(member.contents, "int32 extent"));
		TEST_PASSED();
	}

	void LspFeaturesTest::TestDefinitionOfGlobalVariableUseJumpsToDeclaration()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		auto usePos = Testing::Lsp::PositionOf(kFixture, "g_counter", 2);

		auto locations = Definition::At(session, uri, usePos);
		TEST_ASSERT_EQ(locations.Count(), 1u);

		// Must land on the *declaration* (first occurrence), not the use.
		auto declPos = Testing::Lsp::PositionOf(kFixture, "g_counter", 1);
		TEST_ASSERT_EQ(locations[0].range.start.line, declPos.line);
		TEST_ASSERT_EQ(locations[0].range.start.character, declPos.character);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestDefinitionOfFunctionCallJumpsToDefinition()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		auto usePos = Testing::Lsp::PositionOf(kFixture, "Add", 2);

		auto locations = Definition::At(session, uri, usePos);
		TEST_ASSERT_EQ(locations.Count(), 1u);

		auto declPos = Testing::Lsp::PositionOf(kFixture, "Add", 1);
		TEST_ASSERT_EQ(locations[0].range.start.line, declPos.line);
		TEST_ASSERT_EQ(locations[0].range.start.character, declPos.character);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestDefinitionOfClassFieldUseJumpsToField()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		auto usePos = Testing::Lsp::PositionOf(kFixture, "x", 2); // p.x in "p.x = 5"

		auto locations = Definition::At(session, uri, usePos);
		TEST_ASSERT_EQ(locations.Count(), 1u);

		// The field declaration "int32 x;" is the first occurrence of "x".
		auto declPos = Testing::Lsp::PositionOf(kFixture, "x", 1);
		TEST_ASSERT_EQ(locations[0].range.start.line, declPos.line);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestDefinitionOnNonSymbolReturnsEmpty()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");

		auto locations = Definition::At(session, uri, BeyondFile());
		TEST_ASSERT_EQ(locations.Count(), 0u);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestReferencesFindsAllUsesOfGlobalVariable()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// The *use* of g_counter (2nd occurrence).
		auto usePos = Testing::Lsp::PositionOf(kFixture, "g_counter", 2);

		auto locations = References::At(session, uri, usePos);
		TEST_ASSERT(locations.Count() >= 1u);

		// One of the references must be exactly the use we queried.
		bool foundUse = false;
		for (const auto& loc : locations)
			if (loc.range.start.line == usePos.line && loc.range.start.character == usePos.character)
				foundUse = true;
		TEST_ASSERT(foundUse);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestReferencesOnFunctionCallFindsCalleeUse()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// The callee "Add" in the call (2nd occurrence; 1st is the definition).
		auto usePos = Testing::Lsp::PositionOf(kFixture, "Add", 2);

		auto locations = References::At(session, uri, usePos);
		TEST_ASSERT(locations.Count() >= 1u);

		bool foundUse = false;
		for (const auto& loc : locations)
			if (loc.range.start.line == usePos.line && loc.range.start.character == usePos.character)
				foundUse = true;
		TEST_ASSERT(foundUse);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestReferencesOnNonSymbolReturnsEmpty()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");

		auto locations = References::At(session, uri, BeyondFile());
		TEST_ASSERT_EQ(locations.Count(), 0u);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestDocumentHighlightMarksAllOccurrences()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// The *use* of g_counter (2nd occurrence).
		auto usePos = Testing::Lsp::PositionOf(kFixture, "g_counter", 2);

		auto highlights = DocumentHighlight::At(session, uri, usePos);
		TEST_ASSERT(highlights.Count() >= 1u);

		bool foundUse = false;
		for (const auto& h : highlights)
			if (h.range.start.line == usePos.line && h.range.start.character == usePos.character)
				foundUse = true;
		TEST_ASSERT(foundUse);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestDocumentHighlightKindsMatchVisualStudio()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// The *use* of g_counter (2nd occurrence; 1st is the declaration).
		auto usePos = Testing::Lsp::PositionOf(kFixture, "g_counter", 2);

		auto highlights = DocumentHighlight::At(session, uri, usePos);
		TEST_ASSERT_EQ(highlights.Count(), 2u); // exactly: the declaration (Write) + this one use (Read)

		int writes = 0;
		int reads = 0;
		bool writeIsDeclarationLine = false;
		for (const auto& h : highlights)
		{
			if (h.kind == C4LSP::Protocol::HighlightKind::Write) { ++writes; if (h.range.start.line == 0 && h.range.start.character == 6) writeIsDeclarationLine = true; } // "int32 g_counter" col 6
			else if (h.kind == C4LSP::Protocol::HighlightKind::Read) { ++reads; TEST_ASSERT(h.range.start.line == usePos.line); }
		}
		TEST_ASSERT_EQ((uint32)writes, (uint32)1u);
		TEST_ASSERT(writeIsDeclarationLine); // the Write highlight sits on `g_counter` at its declaration site
		TEST_ASSERT_EQ((uint32)reads, (uint32)1u);     // every other occurrence is a Read
		TEST_PASSED();
	}

	void LspFeaturesTest::TestDocumentHighlightOnNonSymbolReturnsEmpty()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");

		auto highlights = DocumentHighlight::At(session, uri, BeyondFile());
		TEST_ASSERT_EQ(highlights.Count(), 0u);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestCompletionIncludesGlobalFunctionsAndTypes()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// Empty prefix: cursor at the start of a line inside UseIt.
		auto pos = StartOfLine(kFixture, "int32 total = g_counter;");

		auto items = Completion::At(session, uri, pos);
		bool hasAdd = false, hasPoint = false, hasColor = false;
		for (const auto& item : items)
		{
			if (item.label == "Add") hasAdd = true;
			if (item.label == "Point") hasPoint = true;
			if (item.label == "Color") hasColor = true;
		}
		TEST_ASSERT(hasAdd);
		TEST_ASSERT(hasPoint);
		TEST_ASSERT(hasColor);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestCompletionFiltersByTypedPrefix()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// Cursor right after the identifier "Point" in "Point p;" -> prefix is "Point".
		auto pointPos = Testing::Lsp::PositionOf(kFixture, "Point p;");
		pointPos.character += 5; // end of "Point"

		auto items = Completion::At(session, uri, pointPos);
		bool hasPoint = false, hasAdd = false;
		for (const auto& item : items)
		{
			if (item.label == "Point") hasPoint = true;
			if (item.label == "Add") hasAdd = true;
		}
		TEST_ASSERT(hasPoint);   // matches the prefix
		TEST_ASSERT(!hasAdd);    // filtered out by the prefix
		TEST_PASSED();
	}

	void LspFeaturesTest::TestCompletionInsideFunctionIncludesParametersAndLocals()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");
		// Empty prefix inside UseIt, on the "p.x = 5" line.
		auto pos = StartOfLine(kFixture, "p.x = 5;");

		auto items = Completion::At(session, uri, pos);
		bool hasTotal = false, hasAdd = false;
		for (const auto& item : items)
		{
			if (item.label == "total") hasTotal = true; // local variable
			if (item.label == "Add") hasAdd = true;     // global function
		}
		TEST_ASSERT(hasTotal);
		TEST_ASSERT(hasAdd);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestCompletionInsideClassIncludesMembers()
	{
		const char* program =
			R"(struct Widget {
	int32 count;
	void Touch() {
		count = 1;
	}
};
)";
		auto session = Testing::Lsp::MakeSession("Widget.cf", program);
		auto uri = Testing::Lsp::UriFor("Widget.cf");
		// Empty prefix inside the method body, on the "count = 1;" line.
		auto pos = StartOfLine(program, "count = 1;");

		auto items = Completion::At(session, uri, pos);
		bool hasCount = false;
		for (const auto& item : items)
			if (item.label == "count") hasCount = true; // the enclosing class field
		TEST_ASSERT(hasCount);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestCompletionMemberAccessAfterPunctuationListsOnlyMembers()
	{
		// BUG-296/297: a base right after `(` or `!` was refused as "part of a longer word", a pointer's `->` listed
		// nothing, and member lists were padded with every global. The accessors are dangling inside closed
		// brackets, as an editor that closes pairs sends them on the keystroke.
		const char* program =
			R"(struct Box {
	int32 size;
	bool Empty() { return size == 0; }
};

void Take(int32 n) {}

void Use(Box* b, Box c) {
	Take(b->);
	if (!c.) {}
	int32 n = 2+b->;
}

int32 Next(const Box* b) {
	b->
	return 1;
}

namespace N
{
	class Model { public int32 Owning(int32 at) { return at; } }
	class Space { public Model* model; public int32 At(int32 i) { return i; } }
	static int32 Find(const Space* ws, int32 i) {
		return ws->model->Owning(ws->At(i));
	}
}
)";
		auto session = Testing::Lsp::MakeSession("Punct.cf", program);
		auto uri = Testing::Lsp::UriFor("Punct.cf");
		auto labels = [](const Core::Vector<CompletionItem>& items) {
			std::vector<std::string> sorted;
			for (const auto& item : items)
				sorted.push_back(item.label);
			std::sort(sorted.begin(), sorted.end());
			std::string joined;
			for (const auto& label : sorted)
				joined += (joined.empty() ? "" : ",") + label;
			return joined;
		};
		auto arrow = Testing::Lsp::PositionOf(program, "b->", 1);
		arrow.character += 3;
		auto dot = Testing::Lsp::PositionOf(program, "c.", 1);
		dot.character += 2;
		auto sum = Testing::Lsp::PositionOf(program, "b->", 2);
		sum.character += 3;
		auto before = Testing::Lsp::PositionOf(program, "b->", 3); // a statement follows the dangling accessor
		before.character += 3;
		auto chain = Testing::Lsp::PositionOf(program, "ws->", 1); // BUG-299: a valid chain, in a function of a namespace
		chain.character += 4;
		// After `(` (and no `Take`/`Use`/`Box`), after `!`, after an operator, before the next statement.
		const std::string found = labels(Completion::At(session, uri, arrow)) + " | " + labels(Completion::At(session, uri, dot)) + " | " +
			labels(Completion::At(session, uri, sum)) + " | " + labels(Completion::At(session, uri, before)) + " | " +
			labels(Completion::At(session, uri, chain));
		TEST_ASSERT_EQ(found, std::string("Empty,size | Empty,size | Empty,size | Empty,size | At,model"));
		TEST_PASSED();
	}

	void LspFeaturesTest::TestCompletionMemberAccessDoesNotLeakSiblingFunctionLocal()
	{
		// Regression: two functions declare a local of the SAME name with DIFFERENT types. Member-access
		// completion right after `v.` (a dangling accessor - nothing typed yet, exactly what Monaco sends on
		// the first keystroke) must resolve v from THIS function's own scope chain. The old code flat-scanned
		// every scope in the symbol table innermost-first, so a same-named local in a DIFFERENT (sibling)
		// function could win - the "code suggests members of the wrong object" bug (Vector methods offered on
		// a Driver). The dangling accessor is deliberate: a complete `v.Length()` would resolve via its recorded
		// expression type and pass even without this fix, so it would not catch the regression.
		const char* program =
			R"(struct Vec {
	int32 x;
	float Length() { return 0.0f; }
};

struct Drive {
	void Update(float dt);
	void Brake();
};

void First() {
	Vec v;
	v.
}

void Second(Drive d) {
	Drive v;
	d.
	v.
}
)";
		auto session = Testing::Lsp::MakeSession("Leak.cf", program);
		auto uri = Testing::Lsp::UriFor("Leak.cf");

		auto hasLabel = [](const Core::Vector<CompletionItem>& items, const std::string& label) {
			for (const auto& item : items)
				if (item.label == label)
					return true;
			return false;
		};

		// Caret placed right after the dot of a dangling accessor: PositionOf gives the column of the base
		// identifier, +2 lands just past `.`.
		auto caretAfterDot = [](const std::string& text, const std::string& needle, int occurrence) {
			auto pos = Testing::Lsp::PositionOf(text, needle, occurrence);
			pos.character += 2; // skip `<base>` and the dot
			return pos;
		};

		// (1) THE REGRESSION CATCHER. Caret in First() after `v.` where v is FIRST()'s Vec local. A flat scan of
		// all scopes finds Second()'s same-named Drive local first, so without the fix it would offer Update/Brake
		// and NOT x/Length. With position-aware lexical resolution it must offer First's own Vec members only.
		auto posB = caretAfterDot(program, "v.", 1); // First()
		auto itemsB = Completion::At(session, uri, posB);
		TEST_ASSERT(hasLabel(itemsB, "Length"));   // Vec's method - THIS function's local type
		TEST_ASSERT(hasLabel(itemsB, "x"));        // Vec's field
		TEST_ASSERT(!hasLabel(itemsB, "Update"));  // Drive's method must not leak in from Second()
		TEST_ASSERT(!hasLabel(itemsB, "Brake"));   // same

		// (2) The twin direction. Caret in Second() after `v.` where v is SECOND()'s Drive local. Its own members
		// appear and First()'s Vec-local members do not leak in.
		auto posA = caretAfterDot(program, "v.", 2); // Second()
		auto itemsA = Completion::At(session, uri, posA);
		TEST_ASSERT(hasLabel(itemsA, "Update"));    // Drive's method
		TEST_ASSERT(hasLabel(itemsA, "Brake"));     // Drive's other method
		TEST_ASSERT(!hasLabel(itemsA, "Length"));   // Vec's method must not leak in from First()
		TEST_ASSERT(!hasLabel(itemsA, "x"));        // Vec's field either

		// (3) Parameter scope: completing on a function PARAMETER resolves it lexically too. Caret right after
		// `d.` - d is Second()'s parameter of type Drive.
		auto posC = caretAfterDot(program, "d.", 1); // param in Second()
		auto itemsC = Completion::At(session, uri, posC);
		TEST_ASSERT(hasLabel(itemsC, "Brake"));     // the parameter's own members...
		TEST_ASSERT(!hasLabel(itemsC, "Length"));   // ...and no Vec leak

		// (4) Ordinary completion is unaffected: at a plain caret inside Second(), both its local `v` and param
		// `d` are still offered as candidates (the fix did not break normal candidate collection).
		auto posD = StartOfLine(program, "Drive v;");
		auto itemsD = Completion::At(session, uri, posD);
		TEST_ASSERT(hasLabel(itemsD, "v")); // local variable candidate
		TEST_ASSERT(hasLabel(itemsD, "d")); // parameter candidate

		TEST_PASSED();
	}

	void LspFeaturesTest::TestDocumentSymbolsListsTopLevelDeclarations()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");

		auto symbols = DocumentSymbols::ForDocument(session, uri);
		bool hasCounter = false, hasPoint = false, hasColor = false, hasAdd = false, hasMain = false;
		for (const auto& symbol : symbols)
		{
			if (symbol.name == "g_counter") hasCounter = true;
			if (symbol.name == "Point") hasPoint = true;
			if (symbol.name == "Color") hasColor = true;
			if (symbol.name == "Add") hasAdd = true;
			if (symbol.name == "main") hasMain = true;
		}
		TEST_ASSERT(hasCounter);
		TEST_ASSERT(hasPoint);
		TEST_ASSERT(hasColor);
		TEST_ASSERT(hasAdd);
		TEST_ASSERT(hasMain);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestDocumentSymbolsNestsClassMembers()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("LspFixture.cf");

		auto symbols = DocumentSymbols::ForDocument(session, uri);
		const DocumentSymbol* point = nullptr;
		for (const auto& symbol : symbols)
			if (symbol.name == "Point")
				point = &symbol;

		TEST_ASSERT_NOT_NULL(point);
		bool hasX = false, hasY = false;
		for (const auto& child : point->children)
		{
			if (child.name == "x") hasX = true;
			if (child.name == "y") hasY = true;
		}
		TEST_ASSERT(hasX);
		TEST_ASSERT(hasY);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestWorkspaceSymbolsMatchesQueryCaseInsensitively()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);

		// Lowercase query must still find the type, and every hit carries a real location.
		auto symbols = WorkspaceSymbols::ForQuery(session, "point");
		bool foundPoint = false;
		for (const auto& symbol : symbols)
			if (symbol.name == "Point" && symbol.kind == 5 /* Class */)
				foundPoint = true;
		TEST_ASSERT(foundPoint);

		// Uppercase query matches too, and a non-matching name returns nothing.
		auto upper = WorkspaceSymbols::ForQuery(session, "COUNTER");
		bool foundCounter = false;
		for (const auto& symbol : upper)
			if (symbol.name == "g_counter")
				foundCounter = true;
		TEST_ASSERT(foundCounter);

		auto none = WorkspaceSymbols::ForQuery(session, "zzz_no_such_symbol");
		TEST_ASSERT(none.Count() == 0);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestWorkspaceSymbolsFindsMembersOfNonMatchingParent()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);

		// Querying a field name must find it even though the enclosing struct does not match.
		auto symbols = WorkspaceSymbols::ForQuery(session, "x");
		bool foundXInPoint = false;
		for (const auto& symbol : symbols)
			if (symbol.name == "x" && symbol.containerName == "Point")
				foundXInPoint = true;
		TEST_ASSERT(foundXInPoint);

		// Enum values are surfaced with the enum as their container.
		auto colors = WorkspaceSymbols::ForQuery(session, "green");
		bool foundGreen = false;
		for (const auto& symbol : colors)
			if (symbol.name == "Green" && symbol.kind == 14 /* Constant */ && symbol.containerName == "Color")
				foundGreen = true;
		TEST_ASSERT(foundGreen);
		TEST_PASSED();
	}

	void LspFeaturesTest::TestWorkspaceSymbolsFindsDeclarationsInNonMatchingNamespace()
	{
		const char* source =
			"namespace Game\n"
			"{\n"
			"    namespace Physics\n"
			"    {\n"
			"        class Body { public int mass; }\n"
			"        int Step(int dt) { return dt; }\n"
			"    }\n"
			"}\n"
			"int main() { return 0; }\n";
		auto session = Testing::Lsp::MakeSession("LspNamespaces.cf", source);

		// BUG-302: a query that matches neither namespace still finds what is declared inside them.
		auto symbols = WorkspaceSymbols::ForQuery(session, "bod");
		bool foundBody = false;
		for (const auto& symbol : symbols)
			if (symbol.name == "Body" && symbol.kind == 5 /* Class */ && symbol.containerName == "Game :: Physics")
				foundBody = true;
		TEST_ASSERT(foundBody);

		auto steps = WorkspaceSymbols::ForQuery(session, "step");
		bool foundStep = false;
		for (const auto& symbol : steps)
			if (symbol.name == "Step" && symbol.kind == 12 /* Function */)
				foundStep = true;
		TEST_ASSERT(foundStep);

		// A matching namespace is a symbol itself.
		auto physics = WorkspaceSymbols::ForQuery(session, "phys");
		TEST_ASSERT(physics.Count() == 1 && physics[0].name == "Physics" && physics[0].containerName == "Game");
		TEST_PASSED();
	}

	void LspFeaturesTest::TestWorkspaceSymbolsEmptyQueryReturnsAllTopLevel()
	{
		auto session = Testing::Lsp::MakeSession("LspFixture.cf", kFixture);

		// An empty query returns everything: every top-level declaration of the fixture.
		auto symbols = WorkspaceSymbols::ForQuery(session, "");
		bool hasPoint = false, hasAdd = false, hasMain = false;
		for (const auto& symbol : symbols)
		{
			if (symbol.name == "Point") hasPoint = true;
			if (symbol.name == "Add" && symbol.kind == 12 /* Function */) hasAdd = true;
			if (symbol.name == "main") hasMain = true;
		}
		TEST_ASSERT(hasPoint);
		TEST_ASSERT(hasAdd);
		TEST_ASSERT(hasMain);
		TEST_PASSED();
	}
} // namespace Testing
