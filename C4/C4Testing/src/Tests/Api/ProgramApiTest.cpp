#include "ProgramApiTest.h"
#include "C4Api.h"
#include <string>
#include <vector>

namespace Testing
{
	namespace
	{
		const std::string kRing =
			"namespace Game\n"
			"{\n"
			"\tclass Ring\n"
			"\t{\n"
			"\t\tpublic int32 count;\n"
			"\t\tpublic static int32 Capacity() { return 8; }\n"
			"\t\tpublic void Push(int32 value) { count = count + value; }\n"
			"\t}\n"
			"}\n";

		const std::string kMain =
			"include \"ring.ch\"\n"
			"\n"
			"int32 main()\n"
			"{\n"
			"\tGame::Ring ring;\n"
			"\tring.Push(3);\n"
			"\tint32 total = ring.count;\n"
			"\treturn total;\n"
			"}\n";

		// A program from in-memory sources; closed when it goes out of scope.
		class Program
		{
		public:
			explicit Program(const std::vector<std::pair<std::string, std::string>>& files, uint32_t flags = 0)
		{
				std::vector<c4a_source> sources;
				for (const auto& file : files)
					sources.push_back({ file.first.c_str(), file.second.c_str(), file.second.size() });
				c4a_program_open_options options = {};
				options.program.sources = sources.data();
				options.program.source_count = static_cast<uint32_t>(sources.size());
				options.flags = flags;
				m_Program = c4a_program_open_with(&options);
			}
			~Program() { c4a_program_destroy(m_Program); }
			Program(const Program&) = delete;
			Program& operator=(const Program&) = delete;
			c4a_program* get() const { return m_Program; }

		private:
			c4a_program* m_Program = nullptr;
		};

		const std::string kShapes =
			"namespace Game\n"
			"{\n"
			"\tclass Shape\n"
			"\t{\n"
			"\t\tpublic int32 id;\n"
			"\t\tpublic virtual int32 Sides() const { return 0; }\n"
			"\t}\n"
			"\tclass Square : Shape\n"
			"\t{\n"
			"\t\tpublic int32 side;\n"
			"\t\tpublic override int32 Sides() const { return 4; }\n"
			"\t}\n"
			"}\n";

		Program Ring() { return Program({ { "ring.ch", kRing }, { "main.cf", kMain } }); }

		// The 1-based place of the n-th occurrence of `needle` in `text`.
		c4a_position At(const std::string& text, const std::string& needle, int occurrence = 1)
		{
			size_t found = std::string::npos;
			size_t from = 0;
			for (int i = 0; i < occurrence; ++i)
			{
				found = text.find(needle, from);
				if (found == std::string::npos) return { 0, 0 };
				from = found + needle.size();
			}
			c4a_position at = { 1, 1 };
			for (size_t i = 0; i < found; ++i)
			{
				if (text[i] == '\n') { ++at.line; at.column = 1; }
				else ++at.column;
			}
			return at;
		}

		bool Same(const char* text, const char* expected) { return text && std::string(text) == expected; }
	}

	void ProgramApiTest::ACleanProgramOpensWithoutDiagnostics()
	{
		Program program = Ring();
		TEST_ASSERT(c4a_program_error(program.get()) == nullptr);
		TEST_ASSERT_EQ(c4a_program_diagnostic_count(program.get()), 0u);
		TEST_ASSERT_EQ(c4a_program_bodies_lowered(program.get()), 1);
		TEST_ASSERT_EQ(c4a_api_version(), C4A_API_VERSION);
		TEST_PASSED();
	}

	void ProgramApiTest::DiagnosticsNameTheirFileAndPlace()
	{
		const std::string broken = "int32 main()\n{\n\tint32 x = nothing;\n\treturn x;\n}\n";
		Program program({ { "broken.cf", broken } });
		TEST_ASSERT(c4a_program_diagnostic_count(program.get()) >= 1u);
		c4a_program_diagnostic first;
		TEST_ASSERT(c4a_program_diagnostic_at(program.get(), 0, &first));
		TEST_ASSERT(first.severity == C4A_ERROR);
		TEST_ASSERT(Same(first.file, "broken.cf"));
		TEST_ASSERT_EQ(first.at.line, 3u);
		TEST_ASSERT(first.message && std::string(first.message).find("nothing") != std::string::npos);
		TEST_ASSERT_EQ(c4a_program_bodies_lowered(program.get()), 1); // the error is a body's
		c4a_program_diagnostic none;
		TEST_ASSERT(!c4a_program_diagnostic_at(program.get(), c4a_program_diagnostic_count(program.get()), &none));

		Program syntax({ { "syntax.cf", "int32 main( {\n" } });
		TEST_ASSERT(c4a_program_diagnostic_count(syntax.get()) >= 1u);
		TEST_ASSERT_EQ(c4a_program_bodies_lowered(syntax.get()), 0); // an error of the front end stops before the bodies
		TEST_PASSED();
	}

	void ProgramApiTest::AReferenceResolvesToItsDeclarationInAnotherFile()
	{
		Program program = Ring();
		c4a_symbol push;
		TEST_ASSERT(c4a_program_resolve(program.get(), "main.cf", At(kMain, "Push"), &push));
		TEST_ASSERT(push.kind == C4A_FUNCTION);
		TEST_ASSERT(Same(push.name, "Game::Ring::Push"));
		TEST_ASSERT(Same(push.declared.file, "ring.ch"));
		TEST_ASSERT_EQ(push.declared.start.line, 7u);
		TEST_ASSERT(std::string(push.detail).find("Push(int32 value)") != std::string::npos);

		c4a_symbol count;
		TEST_ASSERT(c4a_program_resolve(program.get(), "main.cf", At(kMain, "count"), &count));
		TEST_ASSERT(count.kind == C4A_VARIABLE);
		TEST_ASSERT(Same(count.name, "Game::Ring::count"));
		TEST_ASSERT(Same(count.type, "int32"));
		TEST_ASSERT_EQ(count.declared.start.line, 5u);

		c4a_symbol ring;
		TEST_ASSERT(c4a_program_resolve(program.get(), "main.cf", At(kMain, "Ring"), &ring));
		TEST_ASSERT(ring.kind == C4A_CLASS);
		TEST_ASSERT(Same(ring.name, "Game::Ring"));
		TEST_PASSED();
	}

	void ProgramApiTest::ADeclarationsOwnNameResolvesToItself()
	{
		Program program = Ring();
		c4a_symbol capacity;
		TEST_ASSERT(c4a_program_resolve(program.get(), "ring.ch", At(kRing, "Capacity"), &capacity));
		TEST_ASSERT(capacity.kind == C4A_FUNCTION);
		TEST_ASSERT(Same(capacity.name, "Game::Ring::Capacity"));
		TEST_ASSERT_EQ(capacity.declared.start.line, 6u);
		TEST_PASSED();
	}

	void ProgramApiTest::NothingResolvesWhereNothingIsNamed()
	{
		Program program = Ring();
		c4a_symbol symbol;
		TEST_ASSERT(!c4a_program_resolve(program.get(), "main.cf", At(kMain, "return"), &symbol));
		TEST_ASSERT(!c4a_program_resolve(program.get(), "main.cf", { 0, 0 }, &symbol));
		TEST_ASSERT(!c4a_program_resolve(program.get(), "main.cf", { 400, 1 }, &symbol));
		TEST_ASSERT(!c4a_program_resolve(program.get(), "other.cf", At(kMain, "Push"), &symbol));
		TEST_ASSERT(c4a_program_type_at(program.get(), "other.cf", At(kMain, "Push")) == nullptr);
		TEST_PASSED();
	}

	void ProgramApiTest::TheTypeOfAnExpressionAndOfADeclarationIsTold()
	{
		Program program = Ring();
		const char* count = c4a_program_type_at(program.get(), "main.cf", At(kMain, "count"));
		TEST_ASSERT(Same(count, "int32"));
		const char* total = c4a_program_type_at(program.get(), "main.cf", At(kMain, "total"));
		TEST_ASSERT(Same(total, "int32"));
		const char* ring = c4a_program_type_at(program.get(), "main.cf", At(kMain, "ring.Push"));
		TEST_ASSERT(ring && std::string(ring).find("Ring") != std::string::npos);
		TEST_PASSED();
	}

	void ProgramApiTest::ReferencesListEveryUseAcrossFiles()
	{
		Program program = Ring();
		c4a_locations* uses = c4a_program_references(program.get(), "main.cf", At(kMain, "count"), 0);
		TEST_ASSERT(uses != nullptr);
		const uint32_t withoutDeclaration = c4a_locations_count(uses);
		TEST_ASSERT_EQ(withoutDeclaration, 3u); // two in Push, one in main
		bool inMain = false;
		for (uint32_t i = 0; i < withoutDeclaration; ++i)
		{
			c4a_location location;
			TEST_ASSERT(c4a_locations_at(uses, i, &location));
			inMain = inMain || Same(location.file, "main.cf");
		}
		TEST_ASSERT(inMain);
		c4a_location beyond;
		TEST_ASSERT(!c4a_locations_at(uses, withoutDeclaration, &beyond));
		c4a_locations_destroy(uses);

		c4a_locations* all = c4a_program_references(program.get(), "main.cf", At(kMain, "count"), 1);
		TEST_ASSERT(all != nullptr);
		TEST_ASSERT_EQ(c4a_locations_count(all), withoutDeclaration + 1);
		c4a_locations_destroy(all);

		TEST_ASSERT(c4a_program_references(program.get(), "main.cf", At(kMain, "return"), 1) == nullptr);
		TEST_PASSED();
	}

	void ProgramApiTest::AnObjectsMembersAreListedByNameAndByPlace()
	{
		Program program = Ring();
		c4a_members* named = c4a_program_members_of(program.get(), "Game::Ring");
		TEST_ASSERT(named != nullptr);
		TEST_ASSERT_EQ(c4a_members_count(named), 3u);
		bool sawCount = false, sawCapacity = false, sawPush = false;
		for (uint32_t i = 0; i < c4a_members_count(named); ++i)
		{
			c4a_member member;
			TEST_ASSERT(c4a_members_at(named, i, &member));
			TEST_ASSERT(Same(member.access, "public"));
			if (Same(member.name, "count")) { sawCount = member.kind == C4A_VARIABLE && !member.is_static && member.declared.start.line == 5; }
			if (Same(member.name, "Capacity")) { sawCapacity = member.kind == C4A_FUNCTION && member.is_static && Same(member.declared.file, "ring.ch"); }
			if (Same(member.name, "Push")) { sawPush = member.kind == C4A_FUNCTION && std::string(member.signature).find("int32 value") != std::string::npos; }
		}
		TEST_ASSERT(sawCount && sawCapacity && sawPush);
		c4a_members_destroy(named);

		c4a_members* byType = c4a_program_members_at(program.get(), "main.cf", At(kMain, "Ring"));
		TEST_ASSERT(byType != nullptr);
		TEST_ASSERT_EQ(c4a_members_count(byType), 3u);
		c4a_members_destroy(byType);
		c4a_members* byValue = c4a_program_members_at(program.get(), "main.cf", At(kMain, "ring.count"));
		TEST_ASSERT(byValue != nullptr);
		TEST_ASSERT_EQ(c4a_members_count(byValue), 3u);
		c4a_members_destroy(byValue);

		TEST_ASSERT(c4a_program_members_of(program.get(), "Game::Missing") == nullptr);
		TEST_ASSERT(c4a_program_members_at(program.get(), "main.cf", At(kMain, "return")) == nullptr);
		TEST_PASSED();
	}

	void ProgramApiTest::ANullProgramOrArgumentAnswersNothing()
	{
		c4a_symbol symbol;
		TEST_ASSERT(!c4a_program_resolve(nullptr, "main.cf", { 1, 1 }, &symbol));
		TEST_ASSERT(c4a_program_type_at(nullptr, "main.cf", { 1, 1 }) == nullptr);
		TEST_ASSERT(c4a_program_references(nullptr, "main.cf", { 1, 1 }, 0) == nullptr);
		TEST_ASSERT(c4a_program_members_of(nullptr, "Game::Ring") == nullptr);
		TEST_ASSERT_EQ(c4a_program_diagnostic_count(nullptr), 0u);
		TEST_ASSERT_EQ(c4a_locations_count(nullptr), 0u);
		TEST_ASSERT_EQ(c4a_members_count(nullptr), 0u);
		TEST_ASSERT(c4a_program_error(nullptr) == nullptr);
		c4a_program_destroy(nullptr);
		c4a_locations_destroy(nullptr);
		c4a_members_destroy(nullptr);
		Program empty({});
		c4a_symbol none;
		TEST_ASSERT(!c4a_program_resolve(empty.get(), nullptr, { 1, 1 }, &none));
		TEST_ASSERT(c4a_program_open(nullptr) != nullptr);
		TEST_PASSED();
	}

	void ProgramApiTest::MembersWithBasesNameTheBaseTheyComeFrom()
	{
		Program program({ { "shapes.ch", kShapes } });
		TEST_ASSERT_EQ(c4a_program_diagnostic_count(program.get()), 0u);
		c4a_members* own = c4a_program_members_of(program.get(), "Game::Square");
		TEST_ASSERT(own != nullptr);
		TEST_ASSERT_EQ(c4a_members_count(own), 2u); // side and Sides: its own only
		c4a_members_destroy(own);

		c4a_members* all = c4a_program_members_with_bases_of(program.get(), "Game::Square");
		TEST_ASSERT(all != nullptr);
		bool sawSide = false, sawId = false;
		uint32_t sides = 0;
		for (uint32_t i = 0; i < c4a_members_count(all); ++i)
		{
			c4a_member member;
			TEST_ASSERT(c4a_members_at(all, i, &member));
			const char* from = c4a_members_inherited_from(all, i);
			if (Same(member.name, "side")) sawSide = from == nullptr;
			if (Same(member.name, "id")) sawId = Same(from, "Game::Shape") && Same(member.declared.file, "shapes.ch");
			if (Same(member.name, "Sides")) { ++sides; TEST_ASSERT(from == nullptr); } // the override hides the base's
		}
		TEST_ASSERT(sawSide && sawId);
		TEST_ASSERT_EQ(sides, 1u);
		TEST_ASSERT(c4a_members_inherited_from(all, c4a_members_count(all)) == nullptr);
		c4a_members_destroy(all);

		c4a_members* ofBase = c4a_program_members_with_bases_of(program.get(), "Game::Shape");
		TEST_ASSERT(ofBase != nullptr);
		TEST_ASSERT_EQ(c4a_members_count(ofBase), 2u); // a type without bases lists its own
		c4a_members_destroy(ofBase);

		TEST_ASSERT(c4a_program_members_with_bases_of(program.get(), "Game::Missing") == nullptr);
		TEST_ASSERT(c4a_program_members_with_bases_of(nullptr, "Game::Square") == nullptr);
		TEST_ASSERT(c4a_members_inherited_from(nullptr, 0) == nullptr);
		TEST_PASSED();
	}

	void ProgramApiTest::ADeclarationsOnlyProgramSkipsTheBodies()
	{
		const std::string broken = "int32 Add(int32 a, int32 b)\n{\n\treturn a + nothing;\n}\n";
		Program lowered({ { "add.cf", broken } });
		TEST_ASSERT_EQ(c4a_program_bodies_lowered(lowered.get()), 1);
		TEST_ASSERT(c4a_program_diagnostic_count(lowered.get()) >= 1u); // the body's error

		Program declarations({ { "add.cf", broken } }, C4A_OPEN_DECLARATIONS_ONLY);
		TEST_ASSERT_EQ(c4a_program_bodies_lowered(declarations.get()), 0);
		TEST_ASSERT_EQ(c4a_program_diagnostic_count(declarations.get()), 0u);
		c4a_symbol add;
		TEST_ASSERT(c4a_program_resolve(declarations.get(), "add.cf", { 1, 7 }, &add)); // the declarations are all there
		TEST_ASSERT(Same(add.name, "Add"));

		Program ring({ { "ring.ch", kRing }, { "main.cf", kMain } }, C4A_OPEN_DECLARATIONS_ONLY);
		c4a_members* members = c4a_program_members_of(ring.get(), "Game::Ring");
		TEST_ASSERT(members != nullptr);
		TEST_ASSERT_EQ(c4a_members_count(members), 3u);
		c4a_members_destroy(members);
		c4a_program* none = c4a_program_open_with(nullptr);
		TEST_ASSERT(none != nullptr);
		c4a_program_destroy(none);
		TEST_PASSED();
	}
}
