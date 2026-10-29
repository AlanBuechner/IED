#include "LspFormattingTest.h"
#include "LspTestSupport.h" // Testing::Lsp::Contains
#include <algorithm>
#include <cctype>
#include <sstream>

using namespace C4LSP;
using Testing::Lsp::Contains;

namespace Testing
{
	std::string LspFormattingTest::Apply(const std::string& text, const std::vector<Protocol::TextEdit>& edits)
	{
		if (edits.empty())
			return text;

		auto sorted = edits; // sort by start position so we can walk left-to-right and splice
		std::stable_sort(sorted.begin(), sorted.end(), [](const Protocol::TextEdit& a, const Protocol::TextEdit& b) {
			if (a.range.start.line != b.range.start.line) return a.range.start.line < b.range.start.line;
			return a.range.start.character < b.range.start.character;
		});

		std::vector<std::string> lines; // one entry per 0-based line index; last is "" when text ends with '\n'
		size_t pos = 0;
		while (true) {
			size_t nl = text.find('\n', pos);
			lines.push_back(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
			if (nl == std::string::npos) break;
			pos = nl + 1;
		}

		std::ostringstream out;
		size_t ei = 0; // index into sorted edits; advances monotonically so zero-width edits can't re-match forever
		for (size_t li = 0; li < lines.size(); ++li) {
			std::string line = lines[li];
			size_t cursor = 0;
			while (ei < sorted.size() && size_t(sorted[ei].range.start.line) == li) {
				const Protocol::TextEdit& e = sorted[ei++]; // consume this edit either way
				size_t startCol = std::min<size_t>(size_t(e.range.start.character), line.size());
				// Insertions don't move the cursor in ORIGINAL coordinates, so a later zero-width
				// edit at an original column beyond the cursor is legitimate and must not be clamped.
				size_t s = (startCol >= cursor) ? startCol : cursor; // clamp only genuine overlap violations
				out << line.substr(cursor, s - cursor);
				cursor = s;
				if (e.range.end.line != li) { out << e.newText; break; } // spans to EOL: newText consumes the rest of this line
				size_t en = std::min<size_t>(size_t(e.range.end.character), line.size());
				out << e.newText;
				cursor = en;
			}
			if (li + 1 < lines.size())
				out << line.substr(cursor) << '\n';
			else
				out << line.substr(cursor); // no trailing newline for the final line
		}
		return out.str();
	}

	void LspFormattingTest::TestIndentationNormalizedToFourSpaces()
	{
		std::string text = "void Run() {\n";
		text += "\t\tif (true) {\n";  // two tabs where four spaces are expected
		text += "      x();\n";        // six spaces where eight are expected
		text += "    }\n";            // already at depth-1: must stay put
		text += "}\n";

		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);
		TEST_ASSERT(out == "void Run() {\n    if (true) {\n        x();\n    }\n}\n");
		TEST_PASSED();
	}

	void LspFormattingTest::TestBinaryOperatorSpacing()
	{
		std::string text = "int32 Add(int a,int b){\n";
		text += "\treturn a+b;\n}\n";

		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);
		TEST_ASSERT(Contains(out, "a + b"));       // binary op gets one space each side... 
		TEST_ASSERT(Contains(out, "int a, int b"));// ...and the comma gets one space after it
		TEST_PASSED();
	}

	void LspFormattingTest::TestNoSpaceAfterOpenParenForCallsButSpaceForControl()
	{
		std::string text = "void Run(){\n";
		text += "\tif( x ){\n";   // `if(` -> `if (`, and the space before ')' is removed by the close-paren rule
		text += "\t\tf( 1 );\n";    // call: no spaces inside parens
		text += "\t}\n}\n";

		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);
		TEST_ASSERT(Contains(out, "if (x) {"));  // control keyword keeps its space; ')' glues to the condition
		TEST_ASSERT(Contains(out, "f(1);"));     // call stays tight
		TEST_PASSED();
	}

	void LspFormattingTest::TestCommentsAreNeverTouched()
	{
		std::string text = "int32 x = 5;   // keep me spaced exactly like this\n";
		text += "/* block comment    with weird spacing */ int32 y;\n";

		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);
		TEST_ASSERT(Contains(out, "// keep me spaced exactly like this")); // trailing-comment gap (three spaces) preserved
		TEST_ASSERT(Contains(out, "/* block comment    with weird spacing */ int32 y;")); // multi-line-style comment text untouched
		TEST_PASSED();
	}

	void LspFormattingTest::TestBlankLinesCollapsedToEmpty()
	{
		std::string text = "int a;\n   \n\t\nint b;\n"; // two blank lines carrying stray indentation

		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);
		TEST_ASSERT(out == "int a;\n\n\nint b;\n");
		TEST_PASSED();
	}

	void LspFormattingTest::TestTrailingWhitespaceStripped()
	{
		std::string text = "void Run() {\n\tint x;\t   \n}\n"; // trailing tab+spaces after `x;`

		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);
		// Default options are spaces/tabSize 4 and the formatter always honors them (no style guessing),
		// so the leading tab becomes four spaces and only trailing whitespace is stripped.
		TEST_ASSERT(out == "void Run() {\n    int x;\n}\n");
		TEST_PASSED();
	}

	void LspFormattingTest::TestIdempotentOnAlreadyFormattedText()
	{
		const std::string good =
			"struct Point {\n"
			"    int32 x;\n"
			"    int32 y;\n"
			"};\n\n"; // 4-space indent: the formatter's native style under default options (spaces, tabSize 4)

		auto edits = Features::Formatting::Format(good, {});
		std::string out = Apply(good, edits);
		TEST_ASSERT(out == good); // an already-formatted document must produce zero visible change
		TEST_PASSED();
	}

	void LspFormattingTest::TestGarbageStillOnlyChangesWhitespace()
	{
		const std::string garbage = "int32 x   =  ;\nvoid f( { }\n"; // parse errors everywhere

		auto edits = Features::Formatting::Format(garbage, {});
		std::string out = Apply(garbage, edits);

		// Token text is never changed: strip all whitespace from both and compare.
		auto stripWs = [](std::string s) { std::string r; for (char c : s) if (!isspace((unsigned char)c)) r += c; return r; };
		TEST_ASSERT(stripWs(out) == stripWs(garbage));

		std::string expected = "int32 x =;\nvoid f({}\n"; // only the stray spaces collapse per the spacing rules; the input has no ')' and token text is never added or removed, so neither does the output
		TEST_ASSERT(out == expected);
		TEST_PASSED();
	}

	void LspFormattingTest::TestCrlfFileFormatsKeepingItsBreaks()
	{
		std::string text = "void Run(){\r\n";
		text += "     int x   = 1+2;\r\n"; // Windows line endings + messy indent/spacing
		text += "}\r\n";

		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);

		// The checkout's breaks are the platform's (.gitattributes): formatting fixes the code, never the breaks, which
		// it could only change on the lines it touches.
		TEST_ASSERT(out == "void Run() {\r\n    int x = 1 + 2;\r\n}\r\n");
		TEST_PASSED();
	}

	void LspFormattingTest::TestCompoundOperatorsStayGluedAndSpacedCorrectly()
	{
		// Regression: the formatter must see compound operators (->, ==, <=) as single tokens exactly like
		// NextCompoundToken does. With atomic tokens `a -> b` was spaced as two binary ops (`- >`) and
		// `x == y` became `= =`.
		std::string text = "void Run() {\n";
		text += "\tint32 x=a==b;\n";      // tight '==' written by the author: one token, spaced per binary-op rules
		text += "\tif (x<=y) { x=x-1; }\n";
		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);

		TEST_ASSERT(Contains(out, "a == b"));   // '==' is one token: spaced as a binary op on both sides
		TEST_ASSERT(!Contains(out, "= ="));     // never split into two '=' tokens
		TEST_ASSERT(Contains(out, "(x <= y)")); // '<=' stays glued and gets its spaces as one operator
		TEST_PASSED();

		std::string arrow = "DemoDriver* d;\nd->Init();\n";
		auto edits2 = Features::Formatting::Format(arrow, {});
		std::string out2 = Apply(arrow, edits2);
		TEST_ASSERT(out2 == "DemoDriver* d;\nd->Init();\n"); // '->' glues on both sides: never `d - >Init()` or `d -> Init()`
		TEST_PASSED();
	}

	void LspFormattingTest::TestSemicolonGluesLeftAndSpacesRight()
	{
		std::string text = "void Run(){\n";
		text += "\tint a=1;int b=a;\n";       // `;` before an identifier opens the next statement with one space...
		text += "\tfor(int32 i=0;i<10;i=i+1){ }\n";  // ...including for-loop separators. The empty `{}` keeps its glued gap (not word-like).
		auto edits = Features::Formatting::Format(text, {});
		std::string out = Apply(text, edits);

		TEST_ASSERT(Contains(out, "int a = 1; int b = a;"));
		// NOTE: `i<10` stays left-tight - bare `<`/`>` are deliberately NOT binary-op spaced because they double as
		// generic delimiters (`Vector<u32>`); a lexer-only formatter cannot tell the two apart, so their spacing is
		// preserved exactly as authored. `<=`/`>=` ARE unambiguous and do get spaced (see TestCompoundOperators).
		TEST_ASSERT(Contains(out, "for (int32 i = 0; i<10; i = i + 1) {}"));
		TEST_PASSED();
	}

	void LspFormattingTest::TestOperatorsAfterClosersAndParensAfterOperators()
	{
		std::string text = "bool Run() {\n";
		text += "\tbool ok = Parse(uri, id) && id.View() == ws.Id();\n"; // already formatted: must not change
		text += "\tproblem =(String(a) +uri) +\"tail\";\n";              // glued after a closer and before '(': spaced
		text += "\tgrid[0]=-1;\n";                                           // binary '=' after ']', unary '-' after '='
		text += "\ttry { Go(); } catch(Error& e) { return (ok); }\n";
		text += "\treturn -x;\n}\n";
		auto edits = Features::Formatting::Format(text, { 4, false });
		std::string out = Apply(text, edits);

		TEST_ASSERT(Contains(out, "bool ok = Parse(uri, id) && id.View() == ws.Id();"));
		TEST_ASSERT(Contains(out, "problem = (String(a) + uri) + \"tail\";"));
		TEST_ASSERT(Contains(out, "grid[0] = -1;"));
		TEST_ASSERT(Contains(out, "catch (Error& e)"));
		TEST_ASSERT(Contains(out, "return (ok);")); // a keyword's '(' is left as written
		TEST_ASSERT(Contains(out, "return -x;"));   // unary after a keyword stays tight
		TEST_ASSERT(Apply(out, Features::Formatting::Format(out, { 4, false })) == out); // idempotent
		TEST_PASSED();
	}

	void LspFormattingTest::TestParenthesizedBodyAfterControlHeaderKeepsItsSpace()
	{
		std::string text = "void Run() {\n";
		text += "\tif (ok) ((T*)p)->m = 1;\n";          // already right: must not change
		text += "\twhile (n > 0)(*q)->Step();\n";        // glued: spaced
		text += "\tfor (;;)((U*)r)->Go();\n";
		text += "\tf(a)(b);\n}\n";                       // a call of a call stays tight
		std::string out = Apply(text, Features::Formatting::Format(text, { 4, false }));
		TEST_ASSERT(Contains(out, "if (ok) ((T*)p)->m = 1;"));
		TEST_ASSERT(Contains(out, "while (n > 0) (*q)->Step();"));
		TEST_ASSERT(Contains(out, "for (;;) ((U*)r)->Go();"));
		TEST_ASSERT(Contains(out, "f(a)(b);"));
		TEST_ASSERT(Apply(out, Features::Formatting::Format(out, { 4, false })) == out); // idempotent
		TEST_PASSED();
	}

	void LspFormattingTest::TestGreaterThanBeforeParenIsNoTemplateCall()
	{
		std::string text = "bool Run() {\n";
		text += "\tbool big = size > (uint64)Limit;\n";   // already right: must not change
		text += "\tbool more = a >(b + 1);\n";            // glued after a spaced '>': spaced
		text += "\tauto box = Make<Box>(2);\n";            // a template call stays tight
		text += "\treturn big;\n}\n";
		std::string out = Apply(text, Features::Formatting::Format(text, { 4, false }));
		TEST_ASSERT(Contains(out, "bool big = size > (uint64)Limit;"));
		TEST_ASSERT(Contains(out, "bool more = a > (b + 1);"));
		TEST_ASSERT(Contains(out, "auto box = Make<Box>(2);"));
		TEST_ASSERT(Apply(out, Features::Formatting::Format(out, { 4, false })) == out); // idempotent
		TEST_PASSED();
	}

	void LspFormattingTest::TestCrlfBreaksAreKept()
	{
		std::string text = "// why\r\n";
		text += "void Run() {  \r\n";          // trailing spaces go, the break stays
		text += "\tint32 x = 1;\r\n";
		text += "\t\t\r\n";                     // a blank line's indentation goes, the break stays
		text += "\t// note\r\n}\r\n";
		std::string out = Apply(text, Features::Formatting::Format(text, { 4, false }));
		TEST_ASSERT(out == "// why\r\nvoid Run() {\r\n\tint32 x = 1;\r\n\r\n\t// note\r\n}\r\n");
		TEST_ASSERT(Features::Formatting::Format(out, { 4, false }).empty()); // a formatted \r\n file is left as it is
		TEST_PASSED();
	}

	void LspFormattingTest::TestLambdaReturnArrowKeepsItsSpaces()
	{
		std::string text = "void Run() {\n";
		text += "\twalk.Run([files](Core::StringView path) -> bool { return true; });\n";
		text += "\tauto f = [](int32 a)->Core::Array<int32>* { return nullptr; };\n";
		text += "\tif (Get()->ok) { p->Next(); }\n}\n";
		auto edits = Features::Formatting::Format(text, { 4, false });
		std::string out = Apply(text, edits);

		TEST_ASSERT(Contains(out, "(Core::StringView path) -> bool {"));      // kept
		TEST_ASSERT(Contains(out, "(int32 a) -> Core::Array<int32>* {"));     // spaced
		TEST_ASSERT(Contains(out, "if (Get()->ok) { p->Next(); }"));           // member access stays glued
		TEST_PASSED();
	}

	void LspFormattingTest::TestBraceLessBodiesAndLambdasIndentOnce()
	{
		const std::string good =
			"void Run()\n"
			"{\n"
			"\tfor (int32 t = 0; t < n; t++)\n"
			"\t\tthreads[t] = Run([m]() {\n"
			"\t\t\tGo(m);\n"
			"\t\t});\n"
			"\tif (a &&\n"
			"\t\tb)\n"
			"\t\tStop();\n"
			"\telse if (c)\n"
			"\t\tfor (x : xs)\n"
			"\t\t\tif (x)\n"
			"\t\t\t{\n"
			"\t\t\t\tKeep(x);\n"
			"\t\t\t}\n"
			"\telse\n"
			"\t\tDone();\n"
			"\tworker = Run([&]() {\n"
			"\t\tSay(1);\n"
			"\t});\n"
			"\tif (ok) {\n"
			"\t\tSay(2);\n"
			"\t} else {\n"
			"\t\tSay(3);\n"
			"\t}\n"
			"\tcontext.Method(Name(\"a\"), [b]() -> int32 {\n" // BUG-418: a line closing part of what its opener left open
			"\t\treturn 1;\n"
			"\t}, problem);\n"
			"\tif (Any([&]() {\n"
			"\t\treturn ok;\n"
			"\t}) && more) {\n"
			"\t\tSay(4);\n"
			"\t}\n"
			"\tLast();\n"
			"}\n";
		TEST_ASSERT(Apply(good, Features::Formatting::Format(good, { 4, false })) == good); // already right: untouched

		std::string flat; // the same text with every line's indentation taken away comes back as it was
		for (size_t i = 0; i < good.size(); ++i)
			if (good[i] != '\t')
				flat += good[i];
		TEST_ASSERT(Apply(flat, Features::Formatting::Format(flat, { 4, false })) == good);
		TEST_PASSED();
	}

	void LspFormattingTest::TestUnterminatedStringAtEofCannotCorruptOffsets()
	{
		// A string literal that runs off the end of the document must not push a token's end past text.size():
		// gap math (b.start - a.end) would underflow and emit an out-of-range edit. The formatter still works,
		// just without touching anything after the opening quote.
		std::string good = "int32 x   = 1;\n";
		auto editsGood = Features::Formatting::Format(good, {});
		TEST_ASSERT(Apply(good, editsGood) == "int32 x = 1;\n"); // sanity: the fixture still formats

		std::string bad = good + "\"unterminated\n";
		auto editsBad = Features::Formatting::Format(bad, {});
		std::string out = Apply(bad, editsBad);
		TEST_ASSERT(Contains(out, "int32 x = 1;")); // the valid line is still fixed...
		TEST_ASSERT(out.find("\"unterminated") != std::string::npos); // ...and the broken literal survives verbatim
		TEST_PASSED();
	}

	void LspFormattingTest::TestSpacesConvertedToTabsWhenClientWantsTabs()
	{
		// The "replace spaces with tabs for basic indentation" behavior: a client that wants tabs
		// (insertSpaces:false, tabSize:4 - the editor's default) gets space-indented code re-indented one
		// tab per block level. Indentation is the ONLY thing converted; token spacing stays VAX style.
		std::string text = "void Run() {\n";
		text += "    if (true) {\n";   // 4 spaces where a tab is expected
		text += "        x();\n";       // 8 spaces where two tabs are expected
		text += "    }\n";             // 4 spaces -> one tab
		text += "}\n";

		auto edits = Features::Formatting::Format(text, { 4, false });
		std::string out = Apply(text, edits);
		TEST_ASSERT(out == "void Run() {\n\tif (true) {\n\t\tx();\n\t}\n}\n");
		TEST_PASSED();
	}

	void LspFormattingTest::TestTabIndentationIdempotentUnderTabMode()
	{
		// Tab-indented input under tab options must be a no-op for indentation (already normalized),
		// so running the formatter twice never churns tabs back and forth.
		std::string text = "void Run() {\n\tif (true) {\n\t\tx();\n\t}\n}\n";
		auto edits = Features::Formatting::Format(text, { 4, false });
		TEST_ASSERT(Apply(text, edits) == text); // byte-identical: idempotent under tab mode

		// And the space-mode equivalent stays in spaces (no cross-conversion when the client wants spaces).
		std::string sp = "void Run() {\n    if (true) {\n        x();\n    }\n}\n";
		auto editsSp = Features::Formatting::Format(sp, { 4, true });
		TEST_ASSERT(Apply(sp, editsSp) == sp); // byte-identical under space mode too
		TEST_PASSED();
	}

	void LspFormattingTest::TestMultiDimInitializerOnlyChangesLeadingWhitespace()
	{
		// The safety contract behind "only replace spaces with tabs when safe": the formatter may only
		// ever touch WHITESPACE, never token text. A multi-line / multi-dimension initializer mixes leading
		// indentation (converted to tabs) with intra-line content, so it is a good stress input for that rule.
		std::string text = "void Run() {\n";
		text += "    int32 grid[2][4] =\n";
		text += "    {\n";
		text += "        { 1,   2,\n";      // alignment spaces inside the initializer: preserve exactly
		text += "          3,   4 },\n";    // column-aligned continuation row: leading run converts, rest stays
		text += "        { 5,   6,\n";
		text += "          7,   8 }\n";
		text += "    };\n";
		text += "}\n";

		auto edits = Features::Formatting::Format(text, { 4, false });
		std::string out = Apply(text, edits);

		// Every non-whitespace character sequence is identical to the input (whitespace-only change proof):
		{
			auto stripWs = [](const std::string& s) {
				std::string r;
				for (char c : s) if (!std::isspace(static_cast<unsigned char>(c))) r += c;
				return r;
			};
			TEST_ASSERT(stripWs(out) == stripWs(text)); // no token text added/removed/reordered
		}
		// The leading indentation runs became tabs at the right depths:
		TEST_ASSERT(Contains(out, "\tint32 grid[2][4] ="));
		TEST_ASSERT(Contains(out, "\t\t{ 1, 2,"));       // tab indent + VAX single-space after comma (see e2e ground truth)
		TEST_ASSERT(Contains(out, "\t};"));

		// "Only replace spaces with tabs for basic indentation": every indented line now starts with a
		// TAB (or is unindented), so no newline may be immediately followed by a space. This proves all
		// leading space-indentation was converted to tabs and none survived as spaces.
		TEST_ASSERT(!Contains(out, "\n "));
		TEST_PASSED();
	}

	void LspFormattingTest::TestAccessLabelsSitAtTheirClassIndentation()
	{
		std::string text = "namespace Game\n{\n";
		text += "\tclass SomeClass\n";
		text += "\t{\n";
		text += "\t\tpublic:\n";            // at the members' level: one tab out
		text += "\t\tpublic void Run();\n"; // a C#-style modifier: a member, not a label
		text += "private: // state\n";      // too far out: back to the class's level
		text += "\t\tint32 m_Count;\n";
		text += "\t\t\tprotected :\n";      // a spaced colon is still a label
		text += "\t\tvoid Grow();\n";
		text += "\t};\n";
		text += "}\n";

		std::string want = "namespace Game\n{\n\tclass SomeClass\n\t{\n\tpublic:\n\t\tpublic void Run();\n";
		want += "\tprivate: // state\n\t\tint32 m_Count;\n\tprotected :\n\t\tvoid Grow();\n\t};\n}\n";
		TEST_ASSERT(Apply(text, Features::Formatting::Format(text, { 4, false })) == want);
		TEST_ASSERT(Features::Formatting::Format(want, { 4, false }).empty());

		std::string spaced = "class SomeClass{\n    public:\n    int x;\n}\n";
		TEST_ASSERT(Apply(spaced, Features::Formatting::Format(spaced, {})) == "class SomeClass{\npublic:\n    int x;\n}\n");
		TEST_PASSED();
	}

	void LspFormattingTest::TestAccessLabelOutsideAnyBlockStaysAtColumnZero()
	{
		std::string text = "public:\nint32 x;\n";
		TEST_ASSERT(Features::Formatting::Format(text, { 4, false }).empty());
		TEST_PASSED();
	}
} // namespace Testing
