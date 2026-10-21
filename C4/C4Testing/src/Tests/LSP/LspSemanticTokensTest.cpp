#include "LspSemanticTokensTest.h"
#include <string>
#include <vector>

using C4LSP::Features::SemanticTokenKind;
using C4LSP::Features::SemanticTokenModifiers;
using C4LSP::Features::SemanticTokens;
using Testing::Lsp::PositionOf;

namespace Testing
{
	// The shared fixture mirrors the one used by the other LSP feature tests (proven to compile).
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

	// Locate the token that starts at the n-th occurrence of `needle` in `text`, if any.
	struct TokFound	{
		bool found = false;
		uint32 kind = 0;
		uint32 mods = 0;
		uint32 startLine = 0;
		uint32 startChar = 0;
		uint32 length = 0;
	};

	static TokFound TokenAt(const C4LSP::Features::SemanticTokensResult& r, const std::string& text,
	                        const std::string& needle, int occurrence)
	{
		auto pos = PositionOf(text, needle, occurrence);
		for (const auto& t : r.tokens)
			if (t.startLine == pos.line && t.startCharacter == pos.character)
				return { true, t.kindId, t.modifiers, t.startLine, t.startCharacter, t.length };
		return {};
	}

	static C4LSP::Server::CompilationSession Make(const char* name, const std::string& text)
	{
		return Testing::Lsp::MakeSession(name, text);
	}

	void LspSemanticTokensTest::TestTokenTypeNamesMatchLegendOrder()
	{
		auto names = SemanticTokens::TokenTypeNames();
		TEST_ASSERT_EQ(names.Count(), (uint32)SemanticTokenKind::Count); // 20 kinds (19 standard + controlFlow extension)
		TEST_ASSERT(names[0] == std::string("namespace"));
		TEST_ASSERT(names[1] == std::string("type"));
		TEST_ASSERT(names[5] == std::string("struct"));
		TEST_ASSERT(names[7] == std::string("parameter"));
		TEST_ASSERT(names[9] == std::string("field"));
		TEST_ASSERT(names[10] == std::string("function"));
		TEST_ASSERT(names[17] == std::string("number"));
		TEST_ASSERT(names[18] == std::string("string"));
		TEST_ASSERT(names[19] == std::string("controlFlow")); // C4 extension kind, appended last so ids stay stable

		auto mods = SemanticTokens::ModifierNames();
		TEST_ASSERT_EQ(mods.Count(), (uint32)6);
		TEST_ASSERT(mods[0] == std::string("declaration"));
		TEST_ASSERT(mods[1] == std::string("definition"));
		TEST_ASSERT(mods[5] == std::string("builtin"));
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestBuiltinTypeKeywordIsTypeKind()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "int32", 1); // the type in "int32 g_counter"
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Type);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestBuiltinTypesGetModifierButUserDefinedDoNot()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto builtinTok = TokenAt(result, kFixture, "int32", 1); // language primitive
		TEST_ASSERT(builtinTok.found);
		TEST_ASSERT((builtinTok.mods & SemanticTokenModifiers::Builtin) != 0);

		auto userDefinedTok = TokenAt(result, kFixture, "Point", 2); // a USE of struct Point (line 17: "Point p;")
		TEST_ASSERT(userDefinedTok.found);
		TEST_ASSERT((userDefinedTok.mods & SemanticTokenModifiers::Builtin) == 0);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestGlobalVariableDeclarationIsVariableWithDefinition()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "g_counter", 1); // the declaration
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Variable);
		TEST_ASSERT((tok.mods & SemanticTokenModifiers::Definition) != 0);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestStructNameIsStructKind()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "Point", 1); // the definition "struct Point"
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Struct);
		TEST_ASSERT((tok.mods & SemanticTokenModifiers::Definition) != 0);

		// A *use* of the struct as a type ("Point p;") keeps its own declaration kind (struct), not the
		// generic `type` gold: user-defined types are colored by what they declare, in any context.
		auto use = TokenAt(result, kFixture, "Point", 2);
		TEST_ASSERT(use.found);
		TEST_ASSERT_EQ(use.kind, (uint32)SemanticTokenKind::Struct);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestEnumNameIsEnumKindAndMembersAreVariables()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "Color", 1); // the definition "enum Color"
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Enum);

		auto member = TokenAt(result, kFixture, "Red", 1); // an enum constant (declaration)
		TEST_ASSERT(member.found);
		TEST_ASSERT_EQ(member.kind, (uint32)SemanticTokenKind::Variable);
		TEST_ASSERT((member.mods & SemanticTokenModifiers::Definition) != 0);

		// A use of the enum constant ("Color c = Green;") is a variable reference.
		auto use = TokenAt(result, kFixture, "Green", 1);
		TEST_ASSERT(use.found);
		TEST_ASSERT_EQ(use.kind, (uint32)SemanticTokenKind::Variable);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestFreeFunctionDefinitionIsFunctionKind()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "Add", 1); // the definition
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Function);
		TEST_ASSERT((tok.mods & SemanticTokenModifiers::Definition) != 0);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestMethodInsideStructIsMethodKind()
	{
		const std::string program =
			R"(struct Widget {
	int32 count;
	void Touch() {
		count = 1;
	}
};
)";
		auto session = Make("Widget.cf", program);
		auto uri = Testing::Lsp::UriFor("Widget.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, program, "Touch", 1); // a method definition inside the struct
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Method);
		TEST_ASSERT((tok.mods & SemanticTokenModifiers::Definition) != 0);

		auto field = TokenAt(result, program, "count", 1); // the field declaration
		TEST_ASSERT(field.found);
		TEST_ASSERT_EQ(field.kind, (uint32)SemanticTokenKind::Field);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestFieldDeclarationIsFieldKind()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "x", 1); // "int32 x;" inside Point
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Field);
		TEST_ASSERT((tok.mods & SemanticTokenModifiers::Definition) != 0);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestFunctionParameterIsVariableKind()
	{
		const std::string program = "int32 Sum(int32 left, int32 right) {\n\treturn left + right;\n}\n";
		auto session = Make("Sum.cf", program);
		auto uri = Testing::Lsp::UriFor("Sum.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// House style: a function parameter is NOT a distinct color from variables. Both the
		// declaration and its references classify as `variable`.
		auto tok = TokenAt(result, program, "left", 1); // the parameter declaration
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Variable);

		// A reference to the parameter is also a variable.
		auto use = TokenAt(result, program, "left", 2);
		TEST_ASSERT(use.found);
		TEST_ASSERT_EQ(use.kind, (uint32)SemanticTokenKind::Variable);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestUseOfGlobalVariableIsVariableWithoutDefinition()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "g_counter", 2); // the use in "int32 total = g_counter;"
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Variable);
		TEST_ASSERT((tok.mods & SemanticTokenModifiers::Definition) == 0); // a reference, not a declaration
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestCallToFreeFunctionIsFunctionKind()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "Add", 2); // the call "total = Add(...)"
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Function);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestMemberAccessFieldReferenceIsFieldKind()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "x", 2); // the member in "p.x = 5;"
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Field);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestControlFlowKeywordsAreControlFlowKind()
	{
		const std::string program = "int32 Pick(int32 n) {\n\tif (n > 0) { return 1; }\n\telse { return 0; }\n}\n";
		auto session = Make("Pick.cf", program);
		auto uri = Testing::Lsp::UriFor("Pick.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// Control-flow keywords get the dedicated `controlFlow` kind (the C4 extension appended to
		// the standard legend) so a theme can paint them distinct from ordinary blue keywords.
		auto tok = TokenAt(result, program, "if", 1);
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::ControlFlow);

		auto ret = TokenAt(result, program, "return", 1);
		TEST_ASSERT(ret.found);
		TEST_ASSERT_EQ(ret.kind, (uint32)SemanticTokenKind::ControlFlow);

		auto elseTok = TokenAt(result, program, "else", 1);
		TEST_ASSERT(elseTok.found);
		TEST_ASSERT_EQ(elseTok.kind, (uint32)SemanticTokenKind::ControlFlow);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestAccessModifiersAndQualifiersAreKeywordKind()
	{
		const std::string program = "struct S {\n\tconst int32 value = 5;\n\tstatic int32 count;\n};\n";
		auto session = Make("S.cf", program);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// In the house style const / static (and access modifiers) are ordinary blue keywords;
		// the purple modifier family is reserved for macros/preprocessor lines.
		auto constTok = TokenAt(result, program, "const", 1);
		TEST_ASSERT(constTok.found);
		TEST_ASSERT_EQ(constTok.kind, (uint32)SemanticTokenKind::Keyword);

		auto staticTok = TokenAt(result, program, "static", 1);
		TEST_ASSERT(staticTok.found);
		TEST_ASSERT_EQ(staticTok.kind, (uint32)SemanticTokenKind::Keyword);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestNumberLiteralIsNumberKind()
	{
		auto session = Make("S.cf", kFixture);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto tok = TokenAt(result, kFixture, "0", 1); // the literal in "int32 g_counter = 0;"
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Number);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestCommentIsCommentKind()
	{
		const std::string program =
			R"(// line comment here
int32 a = 1;
/* block comment
   spanning lines */
int32 b = 2;
)";
		auto session = Make("C.cf", program);
		auto uri = Testing::Lsp::UriFor("C.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		auto line = TokenAt(result, program, "//", 1);
		TEST_ASSERT(line.found);
		TEST_ASSERT_EQ(line.kind, (uint32)SemanticTokenKind::Comment);

		auto block = TokenAt(result, program, "/*", 1);
		TEST_ASSERT(block.found);
		TEST_ASSERT_EQ(block.kind, (uint32)SemanticTokenKind::Comment);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestTokensNeverCrossALine()
	{
		// BUG-298: a line comment was one token through the next line (its length that line's and the next one's),
		// and a block comment one token over all its lines - clients refuse tokens that end past their line.
		const std::string program = "// one\r\n// second\r\nint32 a = 1;\r\n/* block\r\n   end */ int32 b = 2;\r\n";
		auto session = Make("Lines.cf", program);
		auto uri = Testing::Lsp::UriFor("Lines.cf");
		auto result = SemanticTokens::ForDocument(session, uri);
		std::vector<uint32> lengths; // each line's length without its "\r\n"
		for (size_t at = 0; at < program.size();)
		{
			size_t end = program.find("\r\n", at);
			lengths.push_back(static_cast<uint32>(end - at));
			at = end + 2;
		}
		std::vector<uint32> commentLines;
		for (const auto& token : result.tokens)
		{
			TEST_ASSERT(token.startLine < lengths.size());
			TEST_ASSERT(token.startCharacter + token.length <= lengths[token.startLine]);
			if (token.kindId == (uint32)SemanticTokenKind::Comment)
				commentLines.push_back(token.startLine);
		}
		TEST_ASSERT(commentLines == std::vector<uint32>({ 0, 1, 3, 4 })); // each line comment, and the block on both lines
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestStringLiteralIsStringKind()
	{
		const std::string program =
			R"(int32 main() {
	printf("hello world\n");
	return 0;
}
)";
		auto session = Make("Str.cf", program);
		auto uri = Testing::Lsp::UriFor("Str.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// The string literal is tagged with the dedicated `string` kind (a standard LSP token type),
		// not folded into number/keyword. Its span starts at the opening quote of "hello world".
		auto tok = TokenAt(result, program, "\"hello", 1);
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::String);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestIncludeDirectiveIsKeywordKind()
	{
		const std::string program =
			R"(include "missing.ch"
void main() {
	return;
}
)";
		auto session = Make("Inc.cf", program);
		auto uri = Testing::Lsp::UriFor("Inc.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// `include` gets keyword color only when it commits as a directive (next token is a `.ch` string),
		// matching the parser's IncludeDeclarationNode rule.
		auto tok = TokenAt(result, program, "include", 1);
		TEST_ASSERT(tok.found);
		TEST_ASSERT_EQ(tok.kind, (uint32)SemanticTokenKind::Keyword);

		// A bare `include` identifier that is not a directive stays an unresolved variable.
		const std::string plain = "void main() {\n\tinclude(1);\n}\n";
		auto session2 = Make("Plain.cf", plain);
		auto uri2 = Testing::Lsp::UriFor("Plain.cf");
		auto result2 = SemanticTokens::ForDocument(session2, uri2);
		auto tok2 = TokenAt(result2, plain, "include", 1);
		TEST_ASSERT(tok2.found);
		TEST_ASSERT_EQ(tok2.kind, (uint32)SemanticTokenKind::Variable);

		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestUnknownDocumentReturnsNoTokens()
	{
		auto session = Make("Known.cf", kFixture);
		auto unknownUri = Testing::Lsp::UriFor("DoesNotExist.cf");

		auto result = SemanticTokens::ForDocument(session, unknownUri);
		TEST_ASSERT_EQ((uint32)result.tokens.size(), (uint32)0);
		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestQualifiedChainSegmentsKeepDeclarationKinds()
	{
		const char* code =
			R"(namespace Win { class Test { public void DoThing(); } }
void Win::Test::DoThing() { }
int32 main() { Win::Test::DoThing(); return 0; })";

		auto session = Make("Q.cf", code);
		auto uri = Testing::Lsp::UriFor("Q.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// Definition line `void Win::Test::DoThing()`: owner segments keep their own declaration kinds; only the
		// LAST segment is the method definition (regression: it used to be stamped function+definition).
		auto defWin = TokenAt(result, code, "Win", 2);
		TEST_ASSERT(defWin.found);
		TEST_ASSERT_EQ(defWin.kind, (uint32)SemanticTokenKind::Namespace);
		auto defTest = TokenAt(result, code, "Test", 2); // class Test (1), Win::Test in the definition (2)
		TEST_ASSERT(defTest.found);
		TEST_ASSERT_EQ(defTest.kind, (uint32)SemanticTokenKind::Class);
		auto defDoThing = TokenAt(result, code, "DoThing", 2); // declaration (1), out-of-line definition (2)
		TEST_ASSERT(defDoThing.found);
		TEST_ASSERT_EQ(defDoThing.kind, (uint32)SemanticTokenKind::Method);
		TEST_ASSERT(defDoThing.mods & SemanticTokenModifiers::Definition);

		// Call site `Win::Test::DoThing()` inside main: the namespace prefix must read as a namespace and not be
		// clobbered by the inner identifier node's plain reference lookup (regression: it rendered as variable).
		auto callWin = TokenAt(result, code, "Win", 3); // definition line (2), call site (3)
		TEST_ASSERT(callWin.found);
		TEST_ASSERT_EQ(callWin.kind, (uint32)SemanticTokenKind::Namespace);
		auto callTest = TokenAt(result, code, "Test", 3);
		TEST_ASSERT(callTest.found);
		TEST_ASSERT_EQ(callTest.kind, (uint32)SemanticTokenKind::Class);
		auto callDoThing = TokenAt(result, code, "DoThing", 3);
		TEST_ASSERT(callDoThing.found);
		TEST_ASSERT_EQ(callDoThing.kind, (uint32)SemanticTokenKind::Method);

		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestTypeInsideNamespaceKeepsItsOwnKindInChains()
	{
		const char* code =
			R"(namespace Engine { struct Thing { int32 id; } }
Engine::Thing t;
int32 main() { return t.id; })";

		auto session = Make("T.cf", code);
		auto uri = Testing::Lsp::UriFor("T.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// `Engine` stays a namespace (a genuine chain prefix).
		auto engine = TokenAt(result, code, "Engine", 2); // declaration line (1), use site (2)
		TEST_ASSERT(engine.found);
		TEST_ASSERT_EQ(engine.kind, (uint32)SemanticTokenKind::Namespace);

		// `Thing` is a real struct: it must keep its OWN kind even though the chain starts with a namespace.
		// Regression: the namespace-prefix fallback used to win and color this as a namespace too.
		auto thing = TokenAt(result, code, "Thing", 2); // declaration (1), use site in `Engine::Thing t;` (2)
		TEST_ASSERT(thing.found);
		TEST_ASSERT_EQ(thing.kind, (uint32)SemanticTokenKind::Struct);

		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestPreprocessorDirectivesAreKeywordAndMacroNamesPurple()
	{
		const char* code =
			R"(#define MAX_HP 100
int32 main() { int32 hp = MAX_HP; return hp; })";

		auto session = Make("M.cf", code);
		auto uri = Testing::Lsp::UriFor("M.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// The directive line's first token is the swallowed "#define": a NEUTRAL KEYWORD, not a macro.
		auto defTok = TokenAt(result, code, "#define", 1);
		TEST_ASSERT(defTok.found);
		TEST_ASSERT_EQ(defTok.kind, (uint32)SemanticTokenKind::Keyword);

		// The macro NAME is purple at BOTH its definition and its usage site.
		auto nameDef = TokenAt(result, code, "MAX_HP", 1); // on the #define line
		TEST_ASSERT(nameDef.found);
		TEST_ASSERT_EQ(nameDef.kind, (uint32)SemanticTokenKind::Macro);

		auto nameUse = TokenAt(result, code, "MAX_HP", 2); // in `int32 hp = MAX_HP;`
		TEST_ASSERT(nameUse.found);
		TEST_ASSERT_EQ(nameUse.kind, (uint32)SemanticTokenKind::Macro);

		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestMacroNameColorsUsageInAnotherFile()
	{
		// Two in-memory sources on one session: a #define in "defs.cf" must color its name as a macro when it is
		// USED from "main.cf". Proves the cross-file scan, not just per-document coloring.
		C4LSP::Server::CompilationSession session;
		session.RegisterInMemorySource("defs.cf", "#define SHARED 7\n");
		session.RegisterInMemorySource("main.cf", "int32 main() { int32 v = SHARED; return v; }\n");
		session.Recompile();

		auto uri = Testing::Lsp::UriFor("main.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		const std::string mainText = "int32 main() { int32 v = SHARED; return v; }\n";
		auto useTok = TokenAt(result, mainText, "SHARED", 1);
		TEST_ASSERT(useTok.found);
		TEST_ASSERT_EQ(useTok.kind, (uint32)SemanticTokenKind::Macro);

		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestTemplateInstantiationArgumentsKeepTypeColors()
	{
		const std::string code =
			R"(template <class T> class Box {
	T value;
};

struct Vec2 { float x, y; }

int32 main() {
	Box<int32> a;      // primitive arg: type + builtin (blue), NOT variable
	Box<Vec2> b;       // user-defined struct arg: keeps its struct kind (gold)
	Box<T> c;          // template-parameter use: typeParameter
	return 0;
}
)";

		auto session = Make("S.cf", code);
		auto uri = Testing::Lsp::UriFor("S.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// `Box<int32>` - the int32 in the instantiation argument list. Before the fix this read as a plain variable
		// because template-argument QualTypes are never semantic-resolved; it must now match the standalone primitive
		// references elsewhere (type + builtin). `int32` first appears here, so occurrence 1 is unambiguous.
		auto primArg = TokenAt(result, code, "int32", 1); // inside Box<int32>
		TEST_ASSERT(primArg.found);
		TEST_ASSERT_EQ(primArg.kind, (uint32)SemanticTokenKind::Type);
		TEST_ASSERT((primArg.mods & SemanticTokenModifiers::Builtin) != 0);

		// `Box<Vec2>` - the struct argument keeps its declaration kind. Vec2 is defined earlier in the file, so its use
		// as an instantiation argument must still classify by symbol (struct), not fall back to a plain variable/type.
		auto vecArg = TokenAt(result, code, "Vec2", 1); // inside Box<Vec2>
		TEST_ASSERT(vecArg.found);
		TEST_ASSERT_EQ(vecArg.kind, (uint32)SemanticTokenKind::Struct);

		// `Box<T>` - the template parameter use reads as typeParameter. T is declared in <class T>, so occurrence 1 is
		// that declaration; occurrence 2 is the Box<T> argument we care about.
		auto tArg = TokenAt(result, code, "T", 2); // inside Box<T> (occurrence 1 is the `class T` declaration)
		TEST_ASSERT(tArg.found);
		TEST_ASSERT_EQ(tArg.kind, (uint32)SemanticTokenKind::TypeParameter);

		TEST_PASSED();
	}

	void LspSemanticTokensTest::TestSwizzleViewPackedKeywordLanesAndElementType()
	{
		const char* code =
			R"(class PackedVec4 { public union { packed float x, y, z, w; }; }
int32 main() { return 0; })";

		auto session = Make("Sw.cf", code);
		auto uri = Testing::Lsp::UriFor("Sw.cf");
		auto result = SemanticTokens::ForDocument(session, uri);

		// `packed` is lexed as an identifier (the parser commits it by text) and lives on no AST name node.
		// Before the fix it degraded to an unresolved variable at re-lex time; now the walk records its exact span
		// with kind Keyword, matching how `const` / `static` read in the house style.
		auto packedTok = TokenAt(result, code, "packed", 1);
		TEST_ASSERT(packedTok.found);
		TEST_ASSERT_EQ(packedTok.kind, (uint32)SemanticTokenKind::Keyword);

		// Lane names are raw tokens on the node, not VariableDefinition children: they must read as field definitions.
		auto laneX = TokenAt(result, code, "x", 1);
		TEST_ASSERT(laneX.found);
		TEST_ASSERT_EQ(laneX.kind, (uint32)SemanticTokenKind::Field);

		// The element type is never reached by the generic child recursion (SwizzleViewNode has no children), so it
		// must be walked explicitly: `float` keeps its real QualType classification instead of degrading to variable.
		auto floatTok = TokenAt(result, code, "float", 1);
		TEST_ASSERT(floatTok.found);
		TEST_ASSERT_EQ(floatTok.kind, (uint32)SemanticTokenKind::Type);

		// The class name itself still reads as a class definition next door.
		auto cls = TokenAt(result, code, "PackedVec4", 1);
		TEST_ASSERT(cls.found);
		TEST_ASSERT_EQ(cls.kind, (uint32)SemanticTokenKind::Class);

		TEST_PASSED();
	}
} // namespace Testing
