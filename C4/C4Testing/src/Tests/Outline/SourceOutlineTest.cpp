#include "SourceOutlineTest.h"
#include "Outline/SourceOutline.h"
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		SourceOutline Outline(const std::string& text, const std::map<std::string, std::string>& defines = {})
		{
			return OutlineOf(text, defines);
		}

		// The index of the declaration named `name`, or -1.
		int32 Find(const SourceOutline& outline, const std::string& name)
		{
			for (size_t i = 0; i < outline.declarations.size(); ++i)
				if (outline.declarations[i].name == name) return static_cast<int32>(i);
			return -1;
		}
	}

	void SourceOutlineTest::DeclarationsNestUnderWhatHoldsThem()
	{
		const SourceOutline outline = Outline(
			"namespace Game\n"
			"{\n"
			"\tclass Player\n"
			"\t{\n"
			"\t\tpublic int32 health;\n"
			"\t\tpublic void Hit(int32 damage) { health = health - damage; }\n"
			"\t}\n"
			"\tint32 Score();\n"
			"}\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT(outline.diagnostics.empty());
		TEST_ASSERT_EQ(outline.declarations.size(), size_t(6));
		const int32 game = Find(outline, "Game");
		const int32 player = Find(outline, "Player");
		TEST_ASSERT_EQ(game, 0);
		TEST_ASSERT_EQ(outline.declarations[game].parent, -1);
		TEST_ASSERT_EQ(outline.declarations[player].parent, game);
		TEST_ASSERT_EQ(outline.declarations[Find(outline, "health")].parent, player);
		TEST_ASSERT_EQ(outline.declarations[Find(outline, "Hit")].parent, player);
		TEST_ASSERT_EQ(outline.declarations[Find(outline, "Score")].parent, game);
		TEST_ASSERT_EQ(outline.declarations[Find(outline, "main")].parent, -1);
		TEST_ASSERT(Find(outline, "damage") < 0); // a parameter is no declaration of the outline
		TEST_PASSED();
	}

	void SourceOutlineTest::KindsOfTypesAndTheirMembers()
	{
		const SourceOutline outline = Outline(
			"struct Point { public float x; }\n"
			"interface IShape { public virtual float Area() const; }\n"
			"class Shape { }\n"
			"enum Color { Red, Green }\n"
			"union Bits { int32 a; float b; }\n"
			"alias Count = int64;\n"
			"const int32 kLimit = 4;\n");
		TEST_ASSERT(outline.diagnostics.empty());
		TEST_ASSERT(outline.declarations[Find(outline, "Point")].kind == OutlineKind::Struct);
		TEST_ASSERT(outline.declarations[Find(outline, "x")].kind == OutlineKind::Variable);
		TEST_ASSERT(outline.declarations[Find(outline, "IShape")].kind == OutlineKind::Interface);
		TEST_ASSERT(outline.declarations[Find(outline, "Area")].kind == OutlineKind::Function);
		TEST_ASSERT(outline.declarations[Find(outline, "Shape")].kind == OutlineKind::Class);
		TEST_ASSERT(outline.declarations[Find(outline, "Color")].kind == OutlineKind::Enum);
		TEST_ASSERT(outline.declarations[Find(outline, "Red")].kind == OutlineKind::EnumValue);
		TEST_ASSERT_EQ(outline.declarations[Find(outline, "Green")].parent, Find(outline, "Color"));
		TEST_ASSERT(outline.declarations[Find(outline, "Bits")].kind == OutlineKind::Union);
		TEST_ASSERT(outline.declarations[Find(outline, "Count")].kind == OutlineKind::Alias);
		TEST_ASSERT(outline.declarations[Find(outline, "kLimit")].kind == OutlineKind::Variable);
		TEST_PASSED();
	}

	void SourceOutlineTest::AMemberDefinedOutOfItsTypeKeepsItsWrittenName()
	{
		const SourceOutline outline = Outline(
			"class Registry { public void Add(int32 value); }\n"
			"void Registry::Add(int32 value) { }\n");
		TEST_ASSERT(outline.diagnostics.empty());
		const int32 defined = Find(outline, "Registry::Add");
		TEST_ASSERT(defined >= 0);
		TEST_ASSERT_EQ(outline.declarations[defined].parent, -1);
		TEST_ASSERT_EQ(outline.declarations[defined].nameAt.line, 2u);
		TEST_ASSERT_EQ(outline.declarations[defined].nameAt.column, 6u);
		TEST_ASSERT_EQ(outline.declarations[Find(outline, "Add")].parent, Find(outline, "Registry"));
		TEST_PASSED();
	}

	void SourceOutlineTest::PlacesAreTheSourceAsWritten()
	{
		const SourceOutline outline = Outline(
			"#define WIDTH 4\n"
			"\n"
			"int32 Width()\n"
			"{\n"
			"\treturn WIDTH;\n"
			"}\n");
		TEST_ASSERT(outline.diagnostics.empty());
		const OutlineDeclaration& width = outline.declarations[Find(outline, "Width")];
		TEST_ASSERT_EQ(width.start.line, 3u);
		TEST_ASSERT_EQ(width.start.column, 1u);
		TEST_ASSERT_EQ(width.nameAt.line, 3u);
		TEST_ASSERT_EQ(width.nameAt.column, 7u);
		TEST_ASSERT_EQ(width.end.line, 6u);
		TEST_PASSED();
	}

	void SourceOutlineTest::DefinesDecideWhatIsCompiledIn()
	{
		const std::string text = "#ifdef FAST\nint32 Fast();\n#else\nint32 Slow();\n#endif\n#ifdef PLATFORM_WINDOWS\nint32 OnWindows();\n#endif\n";
		const SourceOutline plain = Outline(text);
		TEST_ASSERT(Find(plain, "Fast") < 0 && Find(plain, "Slow") >= 0);
		TEST_ASSERT(Find(plain, "OnWindows") >= 0); // the predefined macros of a build
		const SourceOutline fast = Outline(text, { { "FAST", "1" } });
		TEST_ASSERT(Find(fast, "Fast") >= 0 && Find(fast, "Slow") < 0);
		TEST_ASSERT_EQ(fast.declarations[Find(fast, "Fast")].nameAt.line, 2u);
		TEST_PASSED();
	}

	void SourceOutlineTest::SyntaxErrorsAreDiagnosticsAndTheRestIsListed()
	{
		const SourceOutline outline = Outline("int32 First() { return 1; }\nint32 Broken() { return 1 }\nint32 Last() { return 3; }\n");
		TEST_ASSERT(!outline.diagnostics.empty());
		TEST_ASSERT(outline.diagnostics.front().severity == OutlineSeverity::Error);
		TEST_ASSERT_EQ(outline.diagnostics.front().at.line, 2u);
		TEST_ASSERT(!outline.diagnostics.front().message.empty());
		TEST_ASSERT(Find(outline, "First") >= 0);
		TEST_PASSED();
	}

	void SourceOutlineTest::PreprocessorErrorsComeFirst()
	{
		const SourceOutline outline = Outline("int32 A();\n#error not here\nint32 B( {\n");
		TEST_ASSERT(outline.diagnostics.size() >= 2u);
		TEST_ASSERT_EQ(outline.diagnostics.front().at.line, 2u);
		TEST_ASSERT(outline.diagnostics.front().message.find("not here") != std::string::npos);
		TEST_ASSERT(Find(outline, "A") >= 0);
		TEST_PASSED();
	}

	void SourceOutlineTest::FunctionBodiesAndIncludesDeclareNothing()
	{
		const SourceOutline outline = Outline("include \"other.ch\"\nint32 Run()\n{\n\tint32 local = 2;\n\treturn local;\n}\n");
		TEST_ASSERT(outline.diagnostics.empty());
		TEST_ASSERT_EQ(outline.declarations.size(), size_t(1));
		TEST_ASSERT_EQ(outline.declarations[0].name, std::string("Run"));
		TEST_PASSED();
	}

	void SourceOutlineTest::AnEmptySourceHasNoOutline()
	{
		const SourceOutline empty = Outline("");
		TEST_ASSERT(empty.diagnostics.empty() && empty.declarations.empty());
		const SourceOutline comments = Outline("// only a comment\n/* and another */\n");
		TEST_ASSERT(comments.diagnostics.empty() && comments.declarations.empty());
		TEST_PASSED();
	}
} // namespace Testing
