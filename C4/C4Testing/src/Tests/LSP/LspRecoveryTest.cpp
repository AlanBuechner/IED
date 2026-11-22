#include "LspRecoveryTest.h"
#include "LspTestSupport.h"
#include "Features/Hover.h"
#include "Features/Definition.h"
#include "Features/Completion.h"
#include <set>
#include <string>

using namespace C4LSP::Features;
using Testing::Lsp::Contains;

namespace Testing
{
	namespace
	{
		// A member, a function and a statement that do not parse, among declarations that do.
		const char* kBroken =
			R"(struct Point
{
	public int32 x;
	public int32 y +;
	public int32 Sum() { return x; }
}

int32 Broken( { return 1; }

struct Size
{
	public int32 w;
	public int32 h;
}

int32 Area(Size s) { return s.w * s.h; }

int32 main()
{
	Point p;
	Size s;
	int32 bad = s.w *;
	s.w = 2;
	return Area(s) + p.Sum();
}
)";

		// A type body whose `}` was not typed yet.
		const char* kOpen =
			R"(struct Point
{
	public int32 x;
	public int32 Get() { return x; }

struct Size
{
	public int32 w;
}

int32 Width(Size s) { return s.w; }

int32 main() { Size s; return Width(s); }
)";

		C4LSP::Protocol::Position After(const char* text, const std::string& needle)
		{
			C4LSP::Protocol::Position at = Testing::Lsp::PositionOf(text, needle);
			at.character += static_cast<uint32>(needle.size());
			return at;
		}

		std::set<std::string> Labels(const Core::Vector<CompletionItem>& items)
		{
			std::set<std::string> labels;
			for (const CompletionItem& item : items) labels.insert(item.label);
			return labels;
		}
	}

	void LspRecoveryTest::HoverAfterABrokenDeclaration()
	{
		auto session = Testing::Lsp::MakeSession("Broken.cf", kBroken);
		auto hover = Hover::At(session, Testing::Lsp::UriFor("Broken.cf"), Testing::Lsp::PositionOf(kBroken, "Area(s) +"));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Contains(hover.contents, "int32 Area(Size s)"));
		TEST_PASSED();
	}

	void LspRecoveryTest::DefinitionAfterABrokenDeclaration()
	{
		auto session = Testing::Lsp::MakeSession("Broken.cf", kBroken);
		const auto found = Definition::At(session, Testing::Lsp::UriFor("Broken.cf"), Testing::Lsp::PositionOf(kBroken, "Size s;"));
		TEST_ASSERT_EQ(found.Count(), 1u);
		TEST_ASSERT_EQ(found[0].range.start.line, Testing::Lsp::PositionOf(kBroken, "struct Size").line);
		TEST_PASSED();
	}

	void LspRecoveryTest::CompletionAfterABrokenDeclaration()
	{
		auto session = Testing::Lsp::MakeSession("Broken.cf", kBroken);
		const auto labels = Labels(Completion::At(session, Testing::Lsp::UriFor("Broken.cf"), After(kBroken, "\ts.")));
		TEST_ASSERT(labels.count("w") == 1 && labels.count("h") == 1);
		TEST_PASSED();
	}

	void LspRecoveryTest::MembersOfATypeWithABrokenMember()
	{
		auto session = Testing::Lsp::MakeSession("Broken.cf", kBroken);
		auto sum = Hover::At(session, Testing::Lsp::UriFor("Broken.cf"), Testing::Lsp::PositionOf(kBroken, "Sum();"));
		TEST_ASSERT(sum.found);
		TEST_ASSERT(Contains(sum.contents, "Point::Sum()"));
		const auto labels = Labels(Completion::At(session, Testing::Lsp::UriFor("Broken.cf"), After(kBroken, "p.")));
		TEST_ASSERT(labels.count("x") == 1 && labels.count("Sum") == 1 && labels.count("y") == 0); // `y` did not parse
		TEST_PASSED();
	}

	void LspRecoveryTest::DeclarationsAfterABraceLeftOpen()
	{
		auto session = Testing::Lsp::MakeSession("Open.cf", kOpen);
		auto hover = Hover::At(session, Testing::Lsp::UriFor("Open.cf"), Testing::Lsp::PositionOf(kOpen, "Width(s)"));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Contains(hover.contents, "int32 Width(Size s)")); // a free function, not a member of Point
		const auto found = Definition::At(session, Testing::Lsp::UriFor("Open.cf"), Testing::Lsp::PositionOf(kOpen, "Size s;"));
		TEST_ASSERT_EQ(found.Count(), 1u);
		TEST_ASSERT_EQ(found[0].range.start.line, Testing::Lsp::PositionOf(kOpen, "struct Size").line);
		TEST_PASSED();
	}

	void LspRecoveryTest::EachMistakeIsOneDiagnostic()
	{
		auto session = Testing::Lsp::MakeSession("Broken.cf", kBroken);
		std::set<uint32> lines;
		for (const C4Compiler::CompilationError& error : session.GetCompiler().GetAllDiagnostics()) lines.insert(error.location.line);
		TEST_ASSERT(lines == std::set<uint32>({ 4, 8, 22 })); // the member, the parameter list and the statement: nothing more
		TEST_PASSED();
	}
}
