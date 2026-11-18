#include "LspAnnotationArgumentsTest.h"
#include "LspTestSupport.h"
#include "Features/Completion.h"
#include "Features/Definition.h"
#include "Features/Diagnostics.h"
#include "Features/Hover.h"
#include "Features/References.h"
#include <set>

using namespace C4LSP::Features;

namespace Testing
{
	namespace
	{
		const char* kProgram =
			R"(namespace Render
{
	struct MeshRenderer { public int32 mesh; }
	struct Material { public int32 shader; }
	enum class Pass { Opaque, Glass }
	template<T> struct Slot { public T value; }
}

namespace Limits { const int32 kMax = 8; }

namespace Physics
{
	struct Body { public float mass; }

	[Requires(Render::MeshRenderer, Body), Holds(Render::Slot<int32>), Range(0, Limits::kMax), Kind(Render::Pass::Glass)]
	struct Collider { public int32 shape; }
}

struct Unrelated { public int32 x; }

int32 main() { Physics::Collider c; return c.shape; }
)";

		// The same, with `Render::` just typed in an annotation that does not parse yet.
		const char* kTyping =
			R"(namespace Render
{
	struct MeshRenderer { public int32 mesh; }
	struct Material { public int32 shader; }
}

struct Unrelated { public int32 x; }

[Requires(Render::)]
struct Collider { public int32 shape; }
)";

		const char* kMisspelt =
			R"(namespace Render { struct MeshRenderer { public int32 mesh; } }

[Requires(Render::MeshRendrer)]
struct Collider { public int32 shape; }
)";

		// An annotation the library declares, documented, and its uses.
		const char* kDeclared =
			R"(namespace Lib
{
	// The components a component cannot be without.
	[annotation] struct Requires {}
}

struct Mover { public float speed; }

[Requires(Mover)]
struct Player { public float speed; }

[Lib::Requires(Mover)]
struct Enemy { public float speed; }
)";

		const char* kTypo =
			R"(namespace Lib { [annotation] struct Requires {} }
struct Mover { public float speed; }

[Requiers(Mover)]
struct Player { public float speed; }
)";

		bool SamePlace(const C4LSP::Protocol::Position& a, const C4LSP::Protocol::Position& b)
		{
			return a.line == b.line && a.character == b.character;
		}

		// Where `needle`'s n-th occurrence ends: a caret just typed past it.
		C4LSP::Protocol::Position After(const std::string& text, const std::string& needle, int occurrence = 1)
		{
			C4LSP::Protocol::Position at = Testing::Lsp::PositionOf(text, needle, occurrence);
			at.character += static_cast<uint32>(needle.size());
			return at;
		}

		std::set<std::string> Labels(const Core::Vector<CompletionItem>& items)
		{
			std::set<std::string> labels;
			for (const auto& item : items) labels.insert(item.label);
			return labels;
		}

		bool LandsOn(C4LSP::Server::CompilationSession& session, const C4LSP::Protocol::Position& from, const C4LSP::Protocol::Position& declared)
		{
			auto locations = Definition::At(session, Testing::Lsp::UriFor("Annotated.cf"), from);
			return locations.Count() == 1 && SamePlace(locations[0].range.start, declared);
		}
	}

	void LspAnnotationArgumentsTest::TestDefinitionOfAQualifiedType()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kProgram, "MeshRenderer", 2), Testing::Lsp::PositionOf(kProgram, "MeshRenderer", 1)));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestDefinitionOfANameFromTheNamespace()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kProgram, "Body)"), Testing::Lsp::PositionOf(kProgram, "Body")));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestDefinitionOfATemplateInstance()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kProgram, "Slot", 2), Testing::Lsp::PositionOf(kProgram, "Slot", 1)));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestDefinitionOfAConstant()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kProgram, "kMax", 2), Testing::Lsp::PositionOf(kProgram, "kMax", 1)));
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kProgram, "Glass)"), Testing::Lsp::PositionOf(kProgram, "Glass")));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestHoverNamesTheType()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		auto hover = Hover::At(session, Testing::Lsp::UriFor("Annotated.cf"), Testing::Lsp::PositionOf(kProgram, "MeshRenderer", 2));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "MeshRenderer"));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestReferencesFindTheAnnotation()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		auto references = References::At(session, Testing::Lsp::UriFor("Annotated.cf"), Testing::Lsp::PositionOf(kProgram, "MeshRenderer", 1), true);
		bool inAnnotation = false;
		for (const auto& location : references)
			if (SamePlace(location.range.start, Testing::Lsp::PositionOf(kProgram, "MeshRenderer", 2))) inAnnotation = true;
		TEST_ASSERT(inAnnotation);
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestCompletionAfterANamespace()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		std::set<std::string> labels = Labels(Completion::At(session, Testing::Lsp::UriFor("Annotated.cf"), After(kProgram, "Requires(Render::")));
		TEST_ASSERT(labels.count("MeshRenderer") && labels.count("Material") && labels.count("Pass") && labels.count("Slot"));
		TEST_ASSERT(!labels.count("Unrelated") && !labels.count("Collider") && !labels.count("Render")); // the namespace's names alone
		auto typing = Testing::Lsp::MakeSession("Annotated.cf", kTyping);
		std::set<std::string> typed = Labels(Completion::At(typing, Testing::Lsp::UriFor("Annotated.cf"), After(kTyping, "Requires(Render::")));
		TEST_ASSERT(typed.count("MeshRenderer") && typed.count("Material"));
		TEST_ASSERT(!typed.count("Unrelated"));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestCompletionAfterANamespaceWithAPrefix()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		std::set<std::string> labels = Labels(Completion::At(session, Testing::Lsp::UriFor("Annotated.cf"), After(kProgram, "Requires(Render::Me")));
		TEST_ASSERT(labels.count("MeshRenderer") && !labels.count("Material"));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestCompletionAfterATypeListsItsMembers()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kProgram);
		std::set<std::string> labels = Labels(Completion::At(session, Testing::Lsp::UriFor("Annotated.cf"), After(kProgram, "Kind(Render::Pass::")));
		TEST_ASSERT(labels.count("Opaque") && labels.count("Glass"));
		TEST_ASSERT(!labels.count("MeshRenderer") && !labels.count("Unrelated"));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestAnUnknownNameIsADiagnostic()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kMisspelt);
		const auto diagnostics = Diagnostics::ForDocument(session, Testing::Lsp::UriFor("Annotated.cf"));
		TEST_ASSERT_EQ(diagnostics.Count(), (uint32)1);
		TEST_ASSERT(Testing::Lsp::Contains(diagnostics[0].message, "'Render::MeshRendrer' is neither a constant"));
		TEST_ASSERT(SamePlace(diagnostics[0].range.start, Testing::Lsp::PositionOf(kMisspelt, "Render::MeshRendrer")));
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestADeclaredAnnotationsNameLeadsToItsDeclaration()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kDeclared);
		const auto declared = Testing::Lsp::PositionOf(kDeclared, "Requires", 1);
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kDeclared, "Requires", 2), declared));
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kDeclared, "Requires", 3), declared)); // qualified: its last part
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kDeclared, "Mover)"), Testing::Lsp::PositionOf(kDeclared, "Mover"))); // its argument: the type
		auto hover = Hover::At(session, Testing::Lsp::UriFor("Annotated.cf"), Testing::Lsp::PositionOf(kDeclared, "Requires", 2));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "struct Lib::Requires"));
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "The components a component cannot be without.")); // its doc comment
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestADeclaredAnnotationsUsesAreItsReferences()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kDeclared);
		auto references = References::At(session, Testing::Lsp::UriFor("Annotated.cf"), Testing::Lsp::PositionOf(kDeclared, "Requires", 1), true);
		TEST_ASSERT_EQ(references.Count(), 3u); // the declaration and both uses
		bool plain = false, qualified = false;
		for (const auto& location : references)
		{
			plain = plain || SamePlace(location.range.start, Testing::Lsp::PositionOf(kDeclared, "Requires", 2));
			qualified = qualified || SamePlace(location.range.start, Testing::Lsp::PositionOf(kDeclared, "Requires", 3));
		}
		TEST_ASSERT(plain && qualified);
		TEST_PASSED();
	}

	void LspAnnotationArgumentsTest::TestAMisspeltAnnotationIsADiagnostic()
	{
		auto session = Testing::Lsp::MakeSession("Annotated.cf", kTypo);
		const auto diagnostics = Diagnostics::ForDocument(session, Testing::Lsp::UriFor("Annotated.cf"));
		TEST_ASSERT_EQ(diagnostics.Count(), (uint32)1);
		TEST_ASSERT(Testing::Lsp::Contains(diagnostics[0].message, "unknown annotation 'Requiers': did you mean 'Requires'"));
		TEST_ASSERT(SamePlace(diagnostics[0].range.start, Testing::Lsp::PositionOf(kTypo, "Requiers")));
		TEST_PASSED();
	}
} // namespace Testing
