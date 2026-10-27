#include "LspMemberNavigationTest.h"
#include "LspTestSupport.h"
#include "Features/Definition.h"
#include "Features/Hover.h"
#include "Features/References.h"
#include "Features/DocumentHighlight.h"
#include "Features/Rename.h"

using namespace C4LSP::Features;
using Testing::Lsp::Contains;
using Testing::Lsp::PositionOf;

namespace Testing
{
	namespace
	{
		// "Push" is declared (1) and called through a value (2) and through a pointer field (3); "count" is declared (1)
		// and used in Push (2, 3), Count (4), Each (5) and the out-of-line Has (6); "Ring" is the class (1), the
		// qualifier of Has's definition (2) and a type written in code (3, 4).
		const char* const kTemplates =
			R"(template<typename T>
class Ring
{
	public T[8] items;
	public int64 count;
	public void Push(const T& item) { items[count] = item; count += 1; }
	public int64 Count() const { return count; }
	public template<typename F> void Each(F visit) { for (int64 i = 0; i < count; ++i) visit(items[i]); }
	public bool Has(const T& item) const;
}

template<typename T>
bool Ring<T>::Has(const T& item) const { return count > 0; }

class Holder
{
	public Ring<int32>* ring;
}

int32 main()
{
	Ring<int32> points;
	points.Push(1);
	Holder holder;
	holder.ring = &points;
	holder.ring->Push(2);
	points.Each([](int32 item) -> void { });
	return (int32)points.Count() + (points.Has(1) ? 1 : 0);
}
)";

		// "Square" is declared (1), named through its enum in the enum's namespace (2) and fully qualified (3).
		const char* const kEnums =
			R"(namespace Geo
{
	enum class Shape { Circle, Square }
	enum Tone { Low, High }

	int32 Score(Shape shape)
	{
		int32 total = 0;
		if (shape == Shape::Square) total += 1;
		if (Geo::Shape::Circle == shape) total += 2;
		Tone tone = High;
		if (tone == Geo::Tone::Low) total += 3;
		return total;
	}
}

int32 main() { return Geo::Score(Geo::Shape::Square); }
)";

		const char* const kStatics =
			R"(namespace Scratch
{
	class Counter
	{
		public int32 value;
		public static Counter Make(int32 start) { Counter c; c.value = start; return c; }
	}
}

int32 main()
{
	Scratch::Counter made = Scratch::Counter::Make(3);
	return made.value;
}
)";

		bool Same(const C4LSP::Protocol::Position& a, const C4LSP::Protocol::Position& b)
		{
			return a.line == b.line && a.character == b.character;
		}

		// The one place go-to-definition answers at the n-th `needle`, or none.
		bool DefinitionLandsOn(C4LSP::Server::CompilationSession& session, const char* program, const std::string& file,
			const std::string& needle, int occurrence, const std::string& target, int targetOccurrence)
		{
			auto locations = Definition::At(session, Lsp::UriFor(file), PositionOf(program, needle, occurrence));
			return locations.Count() == 1 && Same(locations[0].range.start, PositionOf(program, target, targetOccurrence));
		}

		std::string HoverAt(C4LSP::Server::CompilationSession& session, const char* program, const std::string& file, const std::string& needle, int occurrence)
		{
			return Hover::At(session, Lsp::UriFor(file), PositionOf(program, needle, occurrence)).contents;
		}
	}

	void LspMemberNavigationTest::TestMethodsOfATemplateInstanceResolve()
	{
		auto session = Lsp::MakeSession("Ring.cf", kTemplates);
		const std::string uri = Lsp::UriFor("Ring.cf");
		TEST_ASSERT(DefinitionLandsOn(session, kTemplates, "Ring.cf", "Push", 2, "Push", 1));
		TEST_ASSERT(DefinitionLandsOn(session, kTemplates, "Ring.cf", "Push", 3, "Push", 1)); // through a pointer field
		TEST_ASSERT(DefinitionLandsOn(session, kTemplates, "Ring.cf", "Count", 2, "Count", 1));
		TEST_ASSERT(Contains(HoverAt(session, kTemplates, "Ring.cf", "Push", 2), "Push(const T& item)"));
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kTemplates, "Push", 2), true).Count(), 3u);
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, PositionOf(kTemplates, "Push", 3)).Count(), 3u);
		auto renamed = Rename::At(session, uri, PositionOf(kTemplates, "Push", 2));
		TEST_ASSERT(renamed.found);
		TEST_ASSERT_EQ(renamed.occurrences.Count(), 3u);
		TEST_PASSED();
	}

	void LspMemberNavigationTest::TestMemberTemplatesResolve()
	{
		auto session = Lsp::MakeSession("Ring.cf", kTemplates);
		const std::string uri = Lsp::UriFor("Ring.cf");
		TEST_ASSERT(DefinitionLandsOn(session, kTemplates, "Ring.cf", "Each", 2, "Each", 1));
		const std::string hover = HoverAt(session, kTemplates, "Ring.cf", "Each", 2);
		TEST_ASSERT(Contains(hover, "Each(F visit)"));
		TEST_ASSERT(!Contains(hover, "main"));
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kTemplates, "Each", 2), true).Count(), 2u);
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, PositionOf(kTemplates, "Each", 2)).Count(), 2u);
		TEST_PASSED();
	}

	void LspMemberNavigationTest::TestTemplateBodiesNameTheirOwnMembers()
	{
		auto session = Lsp::MakeSession("Ring.cf", kTemplates);
		const std::string uri = Lsp::UriFor("Ring.cf");
		TEST_ASSERT(DefinitionLandsOn(session, kTemplates, "Ring.cf", "count", 2, "count", 1)); // in the class
		TEST_ASSERT(DefinitionLandsOn(session, kTemplates, "Ring.cf", "count", 6, "count", 1)); // out of line
		TEST_ASSERT(DefinitionLandsOn(session, kTemplates, "Ring.cf", "items", 3, "items", 1));
		TEST_ASSERT(Contains(HoverAt(session, kTemplates, "Ring.cf", "count", 2), "int64 Ring::count"));
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kTemplates, "count", 1), true).Count(), 6u);
		TEST_ASSERT(DefinitionLandsOn(session, kTemplates, "Ring.cf", "Ring", 2, "Ring", 1));
		TEST_ASSERT(Contains(HoverAt(session, kTemplates, "Ring.cf", "Ring", 2), "class Ring"));
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kTemplates, "Ring", 1), true).Count(), 4u);
		TEST_PASSED();
	}

	void LspMemberNavigationTest::TestEnumMembersResolve()
	{
		auto session = Lsp::MakeSession("Enums.cf", kEnums);
		const std::string uri = Lsp::UriFor("Enums.cf");
		TEST_ASSERT(DefinitionLandsOn(session, kEnums, "Enums.cf", "Square", 2, "Square", 1)); // `Shape::Square` in Geo
		TEST_ASSERT(DefinitionLandsOn(session, kEnums, "Enums.cf", "Square", 3, "Square", 1)); // `Geo::Shape::Square`
		TEST_ASSERT(DefinitionLandsOn(session, kEnums, "Enums.cf", "Circle", 2, "Circle", 1));
		TEST_ASSERT(DefinitionLandsOn(session, kEnums, "Enums.cf", "High", 2, "High", 1));    // an unscoped member, unqualified
		TEST_ASSERT(DefinitionLandsOn(session, kEnums, "Enums.cf", "Low", 2, "Low", 1));      // and through its enum
		const std::string hover = HoverAt(session, kEnums, "Enums.cf", "Square", 2);
		TEST_ASSERT(Contains(hover, "Shape::Square"));
		TEST_ASSERT(!Contains(hover, "Score"));
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kEnums, "Square", 2), true).Count(), 3u);
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, PositionOf(kEnums, "Square", 3)).Count(), 3u);
		TEST_PASSED();
	}

	void LspMemberNavigationTest::TestEnumMembersRenameEverywhere()
	{
		auto session = Lsp::MakeSession("Enums.cf", kEnums);
		const std::string uri = Lsp::UriFor("Enums.cf");
		auto square = Rename::At(session, uri, PositionOf(kEnums, "Square", 2));
		TEST_ASSERT(square.found);
		TEST_ASSERT_EQ(square.occurrences.Count(), 3u);
		bool declarationRenamed = false;
		for (const auto& occurrence : square.occurrences)
			declarationRenamed = declarationRenamed || Same(occurrence.range.start, PositionOf(kEnums, "Square", 1));
		TEST_ASSERT(declarationRenamed);
		auto high = Rename::At(session, uri, PositionOf(kEnums, "High", 2));
		TEST_ASSERT_EQ(high.occurrences.Count(), 2u);
		TEST_PASSED();
	}

	void LspMemberNavigationTest::TestStaticMethodsThroughTheirClassResolve()
	{
		auto session = Lsp::MakeSession("Statics.cf", kStatics);
		const std::string uri = Lsp::UriFor("Statics.cf");
		TEST_ASSERT(DefinitionLandsOn(session, kStatics, "Statics.cf", "Make", 2, "Make", 1));
		TEST_ASSERT(DefinitionLandsOn(session, kStatics, "Statics.cf", "Counter", 5, "Counter", 1)); // its class
		TEST_ASSERT(Contains(HoverAt(session, kStatics, "Statics.cf", "Make", 2), "static Counter Scratch::Counter::Make(int32 start)"));
		TEST_ASSERT_EQ(References::At(session, uri, PositionOf(kStatics, "Make", 2), true).Count(), 2u);
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, PositionOf(kStatics, "Make", 2)).Count(), 2u);
		TEST_PASSED();
	}
} // namespace Testing
