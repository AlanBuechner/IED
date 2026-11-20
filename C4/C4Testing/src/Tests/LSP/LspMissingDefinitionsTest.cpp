#include "LspMissingDefinitionsTest.h"
#include "LspTestSupport.h"
#include "Features/CodeActions.h"
#include "Features/Diagnostics.h"
#include "Features/MissingDefinitions.h"
#include "Protocol/LspJson.h"
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace C4LSP;

namespace Testing
{
	namespace
	{
		using Files = std::vector<std::pair<std::string, std::string>>;

		Server::CompilationSession SessionOf(const Files& files)
		{
			Server::CompilationSession session;
			for (const auto& [name, text] : files)
				session.RegisterInMemorySource(name, text);
			session.Recompile();
			return session;
		}

		const std::string& TextOf(const Files& files, const std::string& name)
		{
			static const std::string none;
			for (const auto& [file, text] : files)
				if (file == name)
					return text;
			return none;
		}

		size_t OffsetOf(const std::string& text, const Protocol::Position& at)
		{
			size_t offset = 0;
			for (uint32 line = 0; line < at.line && offset < text.size(); ++line)
			{
				const size_t next = text.find('\n', offset);
				offset = next == std::string::npos ? text.size() : next + 1;
			}
			return std::min(offset + at.character, text.size());
		}

		// The files with an action's edits made, each file's from its last edit back.
		Files Applied(const Files& files, const Features::CodeAction& action)
		{
			Files out = files;
			for (auto& [name, text] : out)
			{
				std::vector<const Features::CodeActionEdit*> edits;
				for (const auto& edit : action.edits)
					if (edit.uri == Testing::Lsp::UriFor(name))
						edits.push_back(&edit);
				const std::string before = text;
				std::sort(edits.begin(), edits.end(), [&](const Features::CodeActionEdit* a, const Features::CodeActionEdit* b) {
					return OffsetOf(before, a->range.start) > OffsetOf(before, b->range.start);
				});
				for (const Features::CodeActionEdit* edit : edits)
				{
					const size_t start = OffsetOf(text, edit->range.start);
					text.replace(start, OffsetOf(text, edit->range.end) - start, edit->newText);
				}
			}
			return out;
		}

		Core::Vector<Features::CodeAction> ActionsFor(Server::CompilationSession& session, const std::string& name)
		{
			return Features::CodeActions::ForDocument(session, Testing::Lsp::UriFor(name));
		}

		const Features::CodeAction* Titled(const Core::Vector<Features::CodeAction>& actions, const std::string& title)
		{
			for (const auto& action : actions)
				if (action.title == title)
					return &action;
			return nullptr;
		}

		// The action a document's list titles so, kept apart from the list.
		std::optional<Features::CodeAction> FixOf(Server::CompilationSession& session, const std::string& name, const std::string& title)
		{
			const auto actions = ActionsFor(session, name);
			const Features::CodeAction* found = Titled(actions, title);
			return found ? std::optional<Features::CodeAction>(*found) : std::nullopt;
		}

		std::vector<std::string> MissingIn(Server::CompilationSession& session, const std::string& name)
		{
			std::vector<std::string> out;
			for (const auto& diag : Features::MissingDefinitions::ForDocument(session, Testing::Lsp::UriFor(name)))
				out.push_back(diag.message);
			return out;
		}

		// Every error of the program's files, as "file: message".
		std::vector<std::string> ErrorsOf(Server::CompilationSession& session, const Files& files)
		{
			std::vector<std::string> out;
			for (const auto& [name, text] : files)
				for (const auto& diag : Features::Diagnostics::ForDocument(session, Testing::Lsp::UriFor(name)))
					if (diag.severity == Protocol::DiagnosticSeverity::Error)
						out.push_back(name + ": " + diag.message);
			return out;
		}

		std::string Joined(const std::vector<std::string>& lines)
		{
			std::string out;
			for (const std::string& line : lines)
				out += line + "\n";
			return out;
		}

		// A method of `Game::Thing` declared beside others, as a gameplay script's header declares them.
		std::string ThingHeader(const std::vector<std::string>& methods)
		{
			std::string out = "namespace Game\n{\n\tstruct Thing\n\t{\n\tpublic:\n";
			for (const std::string& method : methods)
				out += "\t\t" + method + "\n";
			return out + "\t}\n}\n";
		}

		const char* const c_OnLoaded = "Function definition for 'OnLoaded' not found";
	} // namespace

	// --- The warning ---------------------------------------------------------------------------------------------------

	void LspMissingDefinitionsTest::TestAMethodWithoutABodyIsReported()
	{
		const Files files = {
			{ "Thing.ch", ThingHeader({ "public void OnLoaded();", "public void OnUpdate();" }) },
			{ "Thing.cf", "include \"Thing.ch\"\n\nnamespace Game\n{\n\tvoid Thing::OnUpdate() {\n\t}\n}\n" },
		};
		auto session = SessionOf(files);
		const auto diags = Features::MissingDefinitions::ForDocument(session, Testing::Lsp::UriFor("Thing.ch"));
		TEST_ASSERT_EQ(diags.Count(), 1u);
		TEST_ASSERT_EQ(diags[0].message, std::string(c_OnLoaded));
		TEST_ASSERT(diags[0].severity == Protocol::DiagnosticSeverity::Warning);
		TEST_ASSERT_EQ(diags[0].source, std::string("c4"));
		const Protocol::Position name = Testing::Lsp::PositionOf(TextOf(files, "Thing.ch"), "OnLoaded");
		TEST_ASSERT_EQ(diags[0].range.start.line, name.line);
		TEST_ASSERT_EQ(diags[0].range.start.character, name.character);
		TEST_ASSERT_EQ(diags[0].range.end.character, name.character + 8u); // the name, as Visual Studio squiggles it
		TEST_ASSERT_EQ(MissingIn(session, "Thing.cf").size(), (size_t)0);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestAFunctionDefinedInAnyFileIsNot()
	{
		const Files files = {
			{ "Util.ch", "namespace U\n{\n\tint32 Add(int32 a, int32 b);\n\tstruct Box\n\t{\n\tpublic:\n\t\tpublic static Box Make();\n"
				"\t\tpublic int32 Get() const;\n\t\t[operator(\"create\")] public void Init();\n\t\tpublic int32 Inline() { return 1; }\n\t}\n"
				"\tstruct Point { public float x; }\n\tfloat Length() extends Point;\n}\n" },
			{ "a.cf", "include \"Util.ch\"\nint32 U::Add(int32 a, int32 b) { return a + b; }\n" },
			{ "b.cf", "include \"Util.ch\"\nnamespace U\n{\n\tBox Box::Make() { return {}; }\n\tint32 Box::Get() const { return 0; }\n"
				"\tvoid Box::Init() { }\n\tfloat Length() extends Point { return x; }\n}\n" },
		};
		auto session = SessionOf(files);
		TEST_ASSERT_EQ(ErrorsOf(session, files).size(), (size_t)0);
		TEST_ASSERT_EQ(MissingIn(session, "Util.ch").size(), (size_t)0);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestPureAndForeignFunctionsNeedNoBody()
	{
		const Files files = {
			{ "Shapes.ch", "interface IShape { float Area(); }\n"
				"abstract class Base\n{\n\tpublic abstract void Run();\n\tpublic virtual void Stop() = 0;\n}\n"
				"[link(\"strlen\")] uint64 c_strlen(const char8* text);\n" },
		};
		auto session = SessionOf(files);
		TEST_ASSERT_EQ(ErrorsOf(session, files).size(), (size_t)0);
		TEST_ASSERT_EQ(MissingIn(session, "Shapes.ch").size(), (size_t)0);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestOnlyTheOverloadWithoutABodyIsReported()
	{
		const std::string text = "namespace N\n{\n\tvoid Put(int32 v);\n\tvoid Put(float v);\n}\nvoid N::Put(int32 v) { }\n";
		auto session = SessionOf({ { "Put.cf", text } });
		const auto diags = Features::MissingDefinitions::ForDocument(session, Testing::Lsp::UriFor("Put.cf"));
		TEST_ASSERT_EQ(diags.Count(), 1u);
		TEST_ASSERT_EQ(diags[0].range.start.line, Testing::Lsp::PositionOf(text, "Put(float").line);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestTemplatesAreReportedWithAndWithoutUses()
	{
		const std::string box = "template<T> class Box\n{\n\tpublic void Push(T v);\n\tpublic int32 Count();\n\ttemplate<U> public U Later(U u);\n}\n"
			"template<T> public int32 Box::Count() { return 0; }\n";
		const std::vector<std::string> expected = { "Function definition for 'Push' not found", "Function definition for 'Later' not found" };
		auto unused = SessionOf({ { "Box.cf", box } });
		TEST_ASSERT(MissingIn(unused, "Box.cf") == expected); // no function yet: the bodies are looked for by name
		const Files used = { { "Box.cf", box + "int32 main() { Box<int32> b; return b.Count(); }\n" } };
		auto session = SessionOf(used);
		TEST_ASSERT_EQ(ErrorsOf(session, used).size(), (size_t)0);
		TEST_ASSERT(MissingIn(session, "Box.cf") == expected); // Push's instance has no body; Later is still unused
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestAMemberTemplateDefinedInAnotherFileIsNot()
	{
		const Files files = {
			{ "Tree.ch", "namespace Spatial\n{\n\tstruct Tree\n\t{\n\tpublic:\n\t\ttemplate<T> public T Twice(T v) const;\n"
				"\t\ttemplate<T, U> public T First(T a, U b) const;\n\t\ttemplate<T> public T Missing(T v) const;\n\t}\n}\n" },
			{ "Tree.cf", "include \"Tree.ch\"\nint32 main() { Spatial::Tree t; return t.Twice<int32>(2) + t.First<int32, float>(1, 2.0f); }\n" },
			{ "TreeQueries.cf", "include \"Tree.ch\"\nnamespace Spatial\n{\n\ttemplate<T> public T Tree::Twice(T v) const { return v + v; }\n"
				"\ttemplate<T, U> public T Tree::First(T a, U b) const { return a; }\n}\n" },
		};
		auto session = SessionOf(files);
		TEST_ASSERT_EQ(ErrorsOf(session, files).size(), (size_t)0);
		const std::vector<std::string> expected = { "Function definition for 'Missing' not found" };
		TEST_ASSERT(MissingIn(session, "Tree.ch") == expected);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestTurnedOffReportsNothing()
	{
		const Files files = { { "Thing.ch", ThingHeader({ "public void OnLoaded();" }) }, { "Thing.cf", "include \"Thing.ch\"\n" } };
		auto session = SessionOf(files);
		Features::MissingDefinitions::SetEnabled(false);
		const size_t warnings = MissingIn(session, "Thing.ch").size();
		const size_t fixes = ActionsFor(session, "Thing.ch").Count();
		Features::MissingDefinitions::SetEnabled(true);
		TEST_ASSERT_EQ(warnings, (size_t)0);
		TEST_ASSERT_EQ(fixes, (size_t)0); // no fix without the squiggle it fixes
		TEST_ASSERT_EQ(MissingIn(session, "Thing.ch").size(), (size_t)1);
		TEST_PASSED();
	}

	// --- Where the definition goes -------------------------------------------------------------------------------------

	void LspMissingDefinitionsTest::TestTheDefinitionFollowsThePreviousNeighbour()
	{
		const Files files = {
			{ "Thing.ch", ThingHeader({ "public void A();", "public int32 B(int32 x, float y = 2.0f) const;", "public void C();" }) },
			{ "Thing.cf", Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "\tvoid Thing::A() {", "\t}", "", "\t// C's comment",
				"\tvoid Thing::C() {", "\t}", "}" }) },
		};
		auto session = SessionOf(files);
		const auto actions = ActionsFor(session, "Thing.ch");
		const Features::CodeAction* fix = Titled(actions, "Create definition of 'B' in Thing.cf");
		TEST_ASSERT_NOT_NULL(fix);
		TEST_ASSERT(fix->preferred);
		TEST_ASSERT_EQ(fix->edits.Count(), 1u);
		TEST_ASSERT_EQ(fix->edits[0].uri, Testing::Lsp::UriFor("Thing.cf"));
		TEST_ASSERT_EQ(fix->diagnostics.Count(), 1u);
		TEST_ASSERT_EQ(fix->diagnostics[0].message, std::string("Function definition for 'B' not found"));
		const std::string expected = Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "\tvoid Thing::A() {", "\t}", "",
			"\tint32 Thing::B(int32 x, float y) const {", "\t\treturn 0;", "\t}", "", "\t// C's comment", "\tvoid Thing::C() {", "\t}", "}" });
		TEST_ASSERT_EQ(TextOf(Applied(files, *fix), "Thing.cf"), expected);
		// The cursor lands on the body's line, before its statement.
		const std::string& inserted = fix->edits[0].newText;
		TEST_ASSERT_EQ(inserted.substr(fix->edits[0].caret, 9), std::string("return 0;"));
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestTheDefinitionPrecedesTheNextNeighbourAndItsComment()
	{
		const Files files = {
			{ "Thing.ch", ThingHeader({ "public void B();", "public void C();" }) },
			{ "Thing.cf", Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "\t// C's comment", "\tvoid Thing::C() {", "\t}", "}" }) },
		};
		auto session = SessionOf(files);
		std::optional<Features::CodeAction> fix = FixOf(session, "Thing.ch", "Create definition of 'B' in Thing.cf");
		TEST_ASSERT(fix.has_value());
		const std::string expected = Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "\tvoid Thing::B() {", "\t\t", "\t}", "",
			"\t// C's comment", "\tvoid Thing::C() {", "\t}", "}" });
		TEST_ASSERT_EQ(TextOf(Applied(files, *fix), "Thing.cf"), expected);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestWithoutNeighboursItEndsTheNamespaceBlock()
	{
		const Files empty = {
			{ "Thing.ch", ThingHeader({ "public void OnLoaded();" }) },
			{ "Thing.cf", Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "}" }) },
		};
		auto session = SessionOf(empty);
		std::optional<Features::CodeAction> fix = FixOf(session, "Thing.ch", "Create definition of 'OnLoaded' in Thing.cf");
		TEST_ASSERT(fix.has_value());
		TEST_ASSERT_EQ(TextOf(Applied(empty, *fix), "Thing.cf"), Joined({ "include \"Thing.ch\"", "", "namespace Game", "{",
			"\tpublic void Thing::OnLoaded() {", "\t\t", "\t}", "}" }));

		// Code above it in the block is kept apart by a blank line.
		const Files helper = {
			{ "Thing.ch", ThingHeader({ "public void OnLoaded();" }) },
			{ "Thing.cf", Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "\tstatic int32 Helper() { return 1; }", "}" }) },
		};
		auto withHelper = SessionOf(helper);
		fix = FixOf(withHelper, "Thing.ch", "Create definition of 'OnLoaded' in Thing.cf");
		TEST_ASSERT(fix.has_value());
		TEST_ASSERT_EQ(TextOf(Applied(helper, *fix), "Thing.cf"), Joined({ "include \"Thing.ch\"", "", "namespace Game", "{",
			"\tstatic int32 Helper() { return 1; }", "", "\tpublic void Thing::OnLoaded() {", "\t\t", "\t}", "}" }));
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestAtTheFileEndItOpensItsNamespaces()
	{
		const std::string expected = Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "\tpublic void Thing::OnLoaded() {", "\t\t", "\t}", "}" });
		for (const std::string& source : { std::string("include \"Thing.ch\"\n"), std::string("include \"Thing.ch\"") })
		{
			const Files files = { { "Thing.ch", ThingHeader({ "public void OnLoaded();" }) }, { "Thing.cf", source } };
			auto session = SessionOf(files);
			std::optional<Features::CodeAction> fix = FixOf(session, "Thing.ch", "Create definition of 'OnLoaded' in Thing.cf");
			TEST_ASSERT(fix.has_value());
			const Files applied = Applied(files, *fix);
			TEST_ASSERT_EQ(TextOf(applied, "Thing.cf"), expected);
			auto after = SessionOf(applied);
			TEST_ASSERT_EQ(ErrorsOf(after, applied).size(), (size_t)0);
			TEST_ASSERT_EQ(MissingIn(after, "Thing.ch").size(), (size_t)0);
		}
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestTheDefinitionIsWrittenAsTheFileWritesThem()
	{
		// The owner's scripts: `public` (and `static`) on every definition, the brace on its own line, four spaces.
		const Files files = {
			{ "Thing.ch", "namespace Game\n{\n    struct Thing\n    {\n    public:\n        public void A();\n        public static int32 Count();\n    }\n}\n" },
			{ "Thing.cf", Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "    public void Thing::A()", "    {", "    }", "}" }) },
		};
		auto session = SessionOf(files);
		std::optional<Features::CodeAction> fix = FixOf(session, "Thing.ch", "Create definition of 'Count' in Thing.cf");
		TEST_ASSERT(fix.has_value());
		const Files applied = Applied(files, *fix);
		TEST_ASSERT_EQ(TextOf(applied, "Thing.cf"), Joined({ "include \"Thing.ch\"", "", "namespace Game", "{", "    public void Thing::A()", "    {",
			"    }", "", "    public static int32 Thing::Count()", "    {", "        return 0;", "    }", "}" }));
		auto after = SessionOf(applied);
		TEST_ASSERT_EQ(ErrorsOf(after, applied).size(), (size_t)0);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestWithoutASourceFileOnlyInPlaceIsOffered()
	{
		auto session = SessionOf({ { "Lonely.ch", ThingHeader({ "public void F();" }) } });
		const auto actions = ActionsFor(session, "Lonely.ch");
		TEST_ASSERT_EQ(actions.Count(), 1u);
		TEST_ASSERT_EQ(actions[0].title, std::string("Create definition of 'F' in place"));
		TEST_ASSERT(!actions[0].preferred);
		TEST_PASSED();
	}

	// --- What it says --------------------------------------------------------------------------------------------------

	void LspMissingDefinitionsTest::TestTheSignatureDropsDefaultsAndKeepsQualifiers()
	{
		const Files files = {
			{ "Sig.ch", "namespace Game\n{\n\tstruct Point { public float x; }\n\tstruct Sig\n\t{\n\tpublic:\n"
				"\t\tpublic int32 Many(int32 first,\n\t\t\tfloat second = 1.0f,\n\t\t\tconst char8* third = \"x\") const noexcept;\n"
				"\t\tpublic static bool Flag();\n\t}\n\tfloat Length() extends Point;\n}\n" },
			{ "Sig.cf", "include \"Sig.ch\"\n" },
		};
		auto session = SessionOf(files);
		const auto actions = ActionsFor(session, "Sig.ch");
		const Features::CodeAction* many = Titled(actions, "Create definition of 'Many' in Sig.cf");
		const Features::CodeAction* flag = Titled(actions, "Create definition of 'Flag' in Sig.cf");
		const Features::CodeAction* length = Titled(actions, "Create definition of 'Length' in Sig.cf");
		TEST_ASSERT_NOT_NULL(many);
		TEST_ASSERT_NOT_NULL(flag);
		TEST_ASSERT_NOT_NULL(length);
		TEST_ASSERT(Testing::Lsp::Contains(many->edits[0].newText, "\tpublic int32 Sig::Many(int32 first, float second, const char8* third) const noexcept {\n\t\treturn 0;\n\t}"));
		TEST_ASSERT(Testing::Lsp::Contains(flag->edits[0].newText, "\tpublic static bool Sig::Flag() {\n\t\treturn false;\n\t}"));
		TEST_ASSERT(Testing::Lsp::Contains(length->edits[0].newText, "\tfloat Length() extends Point {\n\t\treturn 0.0f;\n\t}"));

		const Features::CodeAction* all = Titled(actions, "Create the 3 missing definitions in Sig.cf");
		TEST_ASSERT_NOT_NULL(all);
		TEST_ASSERT_EQ(all->diagnostics.Count(), 3u);
		const Files applied = Applied(files, *all);
		auto after = SessionOf(applied);
		TEST_ASSERT_EQ(ErrorsOf(after, applied).size(), (size_t)0);
		TEST_ASSERT_EQ(MissingIn(after, "Sig.ch").size(), (size_t)0);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestEveryFixCompilesAndClearsTheWarning()
	{
		const std::vector<std::string> methods = { "IsOn", "Speed", "Exact", "Find", "GetMode", "GetData", "Letter", "Run", "Init" };
		const Files files = {
			{ "Rich.ch", "namespace Game\n{\n\tenum class Mode { A, B }\n\tstruct Data { public int32 v; }\n\tstruct Rich\n\t{\n\tpublic:\n"
				"\t\tpublic bool IsOn();\n\t\tpublic float Speed();\n\t\tpublic double Exact();\n\t\tpublic int32* Find();\n"
				"\t\tpublic Mode GetMode();\n\t\tpublic Data GetData();\n\t\tpublic char8 Letter();\n\t\tpublic void Run(int32 times);\n"
				"\t\t[operator(\"create\")] public void Init();\n\t}\n}\n" },
			{ "Rich.cf", "include \"Rich.ch\"\n\nnamespace Game\n{\n}\n" },
		};
		auto session = SessionOf(files);
		TEST_ASSERT_EQ(MissingIn(session, "Rich.ch").size(), methods.size());
		const auto actions = ActionsFor(session, "Rich.ch");
		for (const std::string& method : methods)
		{
			for (const std::string& where : { std::string(" in Rich.cf"), std::string(" in place") })
			{
				const Features::CodeAction* fix = Titled(actions, "Create definition of '" + method + "'" + where);
				TEST_ASSERT_NOT_NULL(fix);
				const Files applied = Applied(files, *fix);
				auto after = SessionOf(applied);
				TEST_ASSERT_EQ(ErrorsOf(after, applied).size(), (size_t)0);
				const std::vector<std::string> left = MissingIn(after, "Rich.ch");
				TEST_ASSERT_EQ(left.size(), methods.size() - 1);
				TEST_ASSERT(std::find(left.begin(), left.end(), "Function definition for '" + method + "' not found") == left.end());
			}
		}
		const Features::CodeAction* all = Titled(actions, "Create the 9 missing definitions in Rich.cf");
		TEST_ASSERT_NOT_NULL(all);
		TEST_ASSERT_EQ(all->edits.Count(), 1u); // all at the block's end, in the order they are declared
		TEST_ASSERT(all->edits[0].newText.find("Rich::IsOn") < all->edits[0].newText.find("Rich::Init"));
		const Files applied = Applied(files, *all);
		auto after = SessionOf(applied);
		TEST_ASSERT_EQ(ErrorsOf(after, applied).size(), (size_t)0);
		TEST_ASSERT_EQ(MissingIn(after, "Rich.ch").size(), (size_t)0);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestAReferenceReturnGetsAnEmptyBody()
	{
		const Files files = { { "Thing.ch", ThingHeader({ "public int32& Slot();" }) }, { "Thing.cf", "include \"Thing.ch\"\n" } };
		auto session = SessionOf(files);
		std::optional<Features::CodeAction> fix = FixOf(session, "Thing.ch", "Create definition of 'Slot' in Thing.cf");
		TEST_ASSERT(fix.has_value());
		TEST_ASSERT(Testing::Lsp::Contains(fix->edits[0].newText, "\tpublic int32& Thing::Slot() {\n\t\t\n\t}"));
		TEST_PASSED(); // no value to return: the compiler asks for one, as for any body that does not return
	}

	void LspMissingDefinitionsTest::TestInPlaceReplacesTheSemicolon()
	{
		const Files files = { { "Thing.ch", ThingHeader({ "public int32 Count() const;" }) } };
		auto session = SessionOf(files);
		std::optional<Features::CodeAction> fix = FixOf(session, "Thing.ch", "Create definition of 'Count' in place");
		TEST_ASSERT(fix.has_value());
		TEST_ASSERT_EQ(TextOf(Applied(files, *fix), "Thing.ch"), ThingHeader({ "public int32 Count() const {\n\t\t\treturn 0;\n\t\t}" }));

		// Beside a body with its brace on its own line, the same.
		const Files allman = { { "Thing.ch", ThingHeader({ "public void Other()", "{", "}", "public void Run();" }) } };
		auto withOther = SessionOf(allman);
		fix = FixOf(withOther, "Thing.ch", "Create definition of 'Run' in place");
		TEST_ASSERT(fix.has_value());
		const Files applied = Applied(allman, *fix);
		TEST_ASSERT_EQ(TextOf(applied, "Thing.ch"), ThingHeader({ "public void Other()", "{", "}", "public void Run()\n\t\t{\n\t\t\t\n\t\t}" }));
		auto after = SessionOf(applied);
		TEST_ASSERT_EQ(ErrorsOf(after, applied).size(), (size_t)0);
		TEST_ASSERT_EQ(MissingIn(after, "Thing.ch").size(), (size_t)0);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestATemplatesDefinitionKeepsItsHeader()
	{
		const Files files = {
			{ "Box.ch", "template<T> class Box\n{\n\tpublic void Push(T v);\n\tpublic int32 Count();\n}\n" },
			{ "Box.cf", "include \"Box.ch\"\n\ntemplate<T> public int32 Box::Count() {\n\treturn 0;\n}\n" },
		};
		auto session = SessionOf(files);
		std::optional<Features::CodeAction> fix = FixOf(session, "Box.ch", "Create definition of 'Push' in Box.cf");
		TEST_ASSERT(fix.has_value());
		const Files applied = Applied(files, *fix);
		// Before Count, its neighbour declared after it, though no use made a function of either yet.
		TEST_ASSERT_EQ(TextOf(applied, "Box.cf"), std::string("include \"Box.ch\"\n\ntemplate<T> public void Box::Push(T v) {\n\t\n}\n\n"
			"template<T> public int32 Box::Count() {\n\treturn 0;\n}\n"));
		Files used = applied;
		used.push_back({ "main.cf", "include \"Box.ch\"\nint32 main() { Box<int32> b; b.Push(1); return b.Count(); }\n" });
		auto after = SessionOf(used);
		TEST_ASSERT_EQ(ErrorsOf(after, used).size(), (size_t)0);
		TEST_ASSERT_EQ(MissingIn(after, "Box.ch").size(), (size_t)0);
		TEST_PASSED();
	}

	void LspMissingDefinitionsTest::TestSnippetEditsPlaceTheCursor()
	{
		TEST_ASSERT_EQ(Protocol::LspJson::SnippetOf("a$b}c\\d", 1), std::string("a$0\\$b\\}c\\\\d"));
		TEST_ASSERT_EQ(Protocol::LspJson::SnippetOf("{}", 2), std::string("{\\}$0"));

		Features::CodeAction action;
		action.title = "Create definition of 'F' in F.cf";
		action.kind = "quickfix";
		action.preferred = true;
		action.diagnostics.Push({ Protocol::Range{}, 2, "Function definition for 'F' not found" });
		Features::CodeActionEdit edit;
		edit.uri = "file:///F.cf";
		edit.newText = "void F() {\n\t\n}";
		edit.caret = 12;
		action.edits.Push(edit);
		Core::Vector<Features::CodeAction> actions;
		actions.Push(action);

		const Json::Value snippets = Protocol::LspJson::CodeActions(actions, true).At(0);
		TEST_ASSERT(snippets.Member("isPreferred").GetBool(false));
		const Json::Value change = snippets.Member("edit").Member("documentChanges").At(0);
		TEST_ASSERT_EQ(std::string(change.Member("textDocument").Member("uri").GetString()), std::string("file:///F.cf"));
		TEST_ASSERT_EQ(std::string(change.Member("edits").At(0).Member("snippet").Member("value").GetString()), std::string("void F() {\n\t$0\n\\}"));
		TEST_ASSERT_EQ(std::string(change.Member("edits").At(0).Member("snippet").Member("kind").GetString()), std::string("snippet"));

		// A client without snippet edits gets the text as it is.
		const Json::Value plain = Protocol::LspJson::CodeActions(actions, false).At(0);
		TEST_ASSERT(!plain.Member("edit").HasMember("documentChanges"));
		TEST_ASSERT_EQ(std::string(plain.Member("edit").Member("changes").Member("file:///F.cf").At(0).Member("newText").GetString()), edit.newText);
		TEST_PASSED();
	}
} // namespace Testing
