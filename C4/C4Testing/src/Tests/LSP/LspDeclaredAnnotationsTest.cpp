#include "LspDeclaredAnnotationsTest.h"
#include "LspTestSupport.h"
#include "Features/Completion.h"
#include "Features/Definition.h"
#include "Features/Diagnostics.h"
#include "Features/DocumentSymbols.h"
#include "Features/Hover.h"
#include "Features/References.h"
#include "Features/Rename.h"
#include "Features/SemanticTokens.h"
#include "Features/SignatureHelp.h"
#include <set>

using namespace C4LSP::Features;

namespace Testing
{
	namespace
	{
		const char* kFile = "Declared.cf";

		// A library's annotations, documented, and their uses.
		const char* kDeclared =
			R"(namespace Lib
{
	/// The components a component cannot be without.
	annotation Requires(typename component, typename... more) on type;
	/// The same, by name.
	annotation Requires(const char* typeName, const char*... more) on type;
	annotation Range(double min, double max) on field, parameter;
	annotation Component on type;
}

struct Mover { public float speed; }

[Lib::Requires(Mover)]
struct Player { public float speed; }

namespace Lib
{
	[Requires("Mover")]
	struct Enemy { [Range(0, 1)] public float speed; }
}

int32 main() { int32[4] values; return values[0]; }
)";

		const char* kTypo =
			R"(annotation Requires(typename component) on type;
struct Mover { public float speed; }

[Requiers(Mover)]
struct Player { public float speed; }
)";

		const char* kWrong =
			R"(annotation Range(double min, double max) on field;
struct Player { [Range(0, "far")] public float speed; }
)";

		bool SamePlace(const C4LSP::Protocol::Position& a, const C4LSP::Protocol::Position& b)
		{
			return a.line == b.line && a.character == b.character;
		}

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

		const CompletionItem* Item(const Core::Vector<CompletionItem>& items, const std::string& label)
		{
			for (const auto& item : items)
				if (item.label == label) return &item;
			return nullptr;
		}

		bool LandsOn(C4LSP::Server::CompilationSession& session, const C4LSP::Protocol::Position& from, const C4LSP::Protocol::Position& declared)
		{
			auto locations = Definition::At(session, Testing::Lsp::UriFor(kFile), from);
			return locations.Count() == 1 && SamePlace(locations[0].range.start, declared);
		}

		// Completion in a session of `text`, the caret just after `marker`.
		Core::Vector<CompletionItem> CompleteAt(const std::string& text, const std::string& marker)
		{
			auto session = Testing::Lsp::MakeSession(kFile, text);
			return Completion::At(session, Testing::Lsp::UriFor(kFile), After(text, marker));
		}
	}

	void LspDeclaredAnnotationsTest::TestANameLeadsToTheOverloadItsArgumentsFit()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kDeclared);
		const auto byType = Testing::Lsp::PositionOf(kDeclared, "Requires", 1);
		const auto byName = Testing::Lsp::PositionOf(kDeclared, "Requires", 2);
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kDeclared, "Requires", 3), byType));  // [Lib::Requires(Mover)]
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kDeclared, "Requires", 4), byName)); // [Requires("Mover")] in namespace Lib
		TEST_ASSERT(LandsOn(session, Testing::Lsp::PositionOf(kDeclared, "Mover)"), Testing::Lsp::PositionOf(kDeclared, "Mover"))); // its argument: the type
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestHoverShowsEveryOverload()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kDeclared);
		auto hover = Hover::At(session, Testing::Lsp::UriFor(kFile), Testing::Lsp::PositionOf(kDeclared, "Requires", 3));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "annotation `Lib::Requires`, on a type"));
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "[Requires(typename component, typename... more)]"));
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "[Requires(const char* typeName, const char*... more)]"));
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "The components a component cannot be without.")); // its doc comment
		auto range = Hover::At(session, Testing::Lsp::UriFor(kFile), Testing::Lsp::PositionOf(kDeclared, "Range", 2));
		TEST_ASSERT(Testing::Lsp::Contains(range.contents, "on a field or a parameter"));
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestHoverOnADeclarationShowsTheAnnotation()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kDeclared);
		auto hover = Hover::At(session, Testing::Lsp::UriFor(kFile), Testing::Lsp::PositionOf(kDeclared, "Requires", 2));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "annotation `Lib::Requires`, on a type"));
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "[Requires(typename component, typename... more)]"));
		TEST_ASSERT(Testing::Lsp::Contains(hover.contents, "The same, by name.")); // this declaration's own doc comment
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestItsUsesAreTheReferencesOfEveryOverload()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kDeclared);
		auto references = References::At(session, Testing::Lsp::UriFor(kFile), Testing::Lsp::PositionOf(kDeclared, "Requires", 1), true);
		TEST_ASSERT_EQ(references.Count(), 4u); // both declarations and both uses
		bool plain = false, qualified = false;
		for (const auto& location : references)
		{
			plain = plain || SamePlace(location.range.start, Testing::Lsp::PositionOf(kDeclared, "Requires", 3));
			qualified = qualified || SamePlace(location.range.start, Testing::Lsp::PositionOf(kDeclared, "Requires", 4));
		}
		TEST_ASSERT(plain && qualified);
		auto fromUse = References::At(session, Testing::Lsp::UriFor(kFile), Testing::Lsp::PositionOf(kDeclared, "Requires", 3), true);
		TEST_ASSERT_EQ(fromUse.Count(), 4u);
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestRenameRenamesEveryDeclarationAndUse()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kDeclared);
		auto rename = Rename::At(session, Testing::Lsp::UriFor(kFile), Testing::Lsp::PositionOf(kDeclared, "Requires", 3));
		TEST_ASSERT(rename.found);
		TEST_ASSERT_EQ(rename.occurrences.Count(), 4u);
		std::set<uint32> lines;
		for (const auto& occurrence : rename.occurrences) lines.insert(occurrence.range.start.line);
		TEST_ASSERT_EQ(lines.size(), (size_t)4);
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestCompletionOffersWhatGoesThere()
	{
		const std::string beforeType = std::string(kDeclared) + "namespace Lib\n{\n[\nstruct Fresh { public int32 v; }\n}\n";
		auto onType = CompleteAt(beforeType, "values[0]; }\nnamespace Lib\n{\n[");
		std::set<std::string> labels = Labels(onType);
		TEST_ASSERT(labels.count("Requires") && labels.count("Component"));
		TEST_ASSERT(!labels.count("Range")); // a field's
		TEST_ASSERT(labels.count("test") && labels.count("group")); // the compiler's own
		const CompletionItem* declared = Item(onType, "Requires");
		TEST_ASSERT(declared && Testing::Lsp::Contains(declared->detail, "[Requires(typename component, typename... more)]"));
		TEST_ASSERT(declared->sortText < "Mover"); // before code's names

		const std::string beforeField = std::string(kDeclared) + "namespace Lib { struct Fresh { [Ra public float v; } }\n";
		std::set<std::string> field = Labels(CompleteAt(beforeField, "[Ra"));
		TEST_ASSERT(field.count("Range") && !field.count("Requires") && !field.count("Component"));

		const std::string inParameters = std::string(kDeclared) + "namespace Lib { void Move([ float d) { } }\n";
		std::set<std::string> parameter = Labels(CompleteAt(inParameters, "Move(["));
		TEST_ASSERT(parameter.count("Range") && !parameter.count("Component"));
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestCompletionAfterAComma()
	{
		const std::string text = std::string(kDeclared) + "namespace Lib { [Component, Re]\nstruct Fresh { public int32 v; } }\n";
		std::set<std::string> labels = Labels(CompleteAt(text, "[Component, Re"));
		TEST_ASSERT(labels.count("Requires") && !labels.count("Range") && !labels.count("Component"));
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestCompletionOfPlacesAfterOn()
	{
		const std::string afterOn = std::string(kDeclared) + "namespace Lib { annotation Tint(double r) on  }\n";
		auto places = CompleteAt(afterOn, "Tint(double r) on ");
		std::set<std::string> labels = Labels(places);
		for (const char* place : { "type", "enum", "union", "field", "method", "function", "parameter", "variable" })
			TEST_ASSERT(labels.count(place));
		TEST_ASSERT_EQ(labels.size(), (size_t)8); // places only: no names from code
		const CompletionItem* field = Item(places, "field");
		TEST_ASSERT(field && field->detail == "the annotation goes on a field");
		TEST_ASSERT(Item(places, "type")->sortText < Item(places, "variable")->sortText); // in the docs' order

		const std::string afterComma = std::string(kDeclared) + "namespace Lib { annotation Tint on field, pa }\n";
		std::set<std::string> second = Labels(CompleteAt(afterComma, "on field, pa"));
		TEST_ASSERT(second.size() == 1 && second.count("parameter"));

		const std::string noParentheses = std::string(kDeclared) + "namespace Lib { annotation Marked on  }\n";
		TEST_ASSERT_EQ(Labels(CompleteAt(noParentheses, "Marked on ")).size(), (size_t)8);

		const std::string notADeclaration = std::string(kDeclared) + "void Turn(int32 on) { int32 x = on ; }\n";
		TEST_ASSERT(!Labels(CompleteAt(notADeclaration, "int32 x = on ")).count("field"));
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestTheDeclarationIsHighlightedAndListed()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kDeclared);
		auto tokens = SemanticTokens::ForDocument(session, Testing::Lsp::UriFor(kFile));
		auto kindAt = [&](const std::string& needle, int occurrence) -> int {
			const auto at = Testing::Lsp::PositionOf(kDeclared, needle, occurrence);
			for (const auto& token : tokens.tokens)
				if (token.startLine == at.line && token.startCharacter == at.character) return static_cast<int>(token.kindId);
			return -1;
		};
		TEST_ASSERT_EQ(kindAt("annotation Range", 1), (int)SemanticTokenKind::Keyword);
		TEST_ASSERT_EQ(kindAt("Range(double", 1), (int)SemanticTokenKind::Macro); // as its uses read
		TEST_ASSERT_EQ(kindAt("on field", 1), (int)SemanticTokenKind::Keyword);
		TEST_ASSERT_EQ(kindAt("parameter;", 1), (int)SemanticTokenKind::Keyword);
		TEST_ASSERT_EQ(kindAt("typename component", 1), (int)SemanticTokenKind::Keyword); // a parameter that takes a type
		TEST_ASSERT_EQ(kindAt("char* typeName", 1), (int)SemanticTokenKind::Type); // any other parameter's type is a type

		auto symbols = DocumentSymbols::ForDocument(session, Testing::Lsp::UriFor(kFile));
		const DocumentSymbol* lib = nullptr;
		for (const auto& symbol : symbols)
			if (symbol.name == "Lib" && !lib) lib = &symbol; // the first, which declares them
		TEST_ASSERT(lib != nullptr);
		int declarations = 0;
		for (const auto& child : lib->children)
			if (child.name == "Range")
			{
				++declarations;
				TEST_ASSERT(child.detail == "annotation (double min, double max) on field, parameter");
			}
			else if (child.name == "Requires") ++declarations;
		TEST_ASSERT_EQ(declarations, 3);
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestCompletionFollowsNamespaces()
	{
		const std::string global = std::string(kDeclared) + "annotation Everywhere on type;\n[\nstruct Fresh { public int32 v; }\n";
		std::set<std::string> outside = Labels(CompleteAt(global, "on type;\n["));
		TEST_ASSERT(outside.count("Everywhere") && !outside.count("Requires") && !outside.count("Component")); // Lib's are Lib's

		const std::string qualified = std::string(kDeclared) + "[Lib::]\nstruct Fresh { public int32 v; }\n";
		auto named = CompleteAt(qualified, "[Lib::");
		std::set<std::string> inLib = Labels(named);
		TEST_ASSERT(inLib.count("Requires") && inLib.count("Component") && !inLib.count("Range")); // Range goes on a field
		TEST_ASSERT(!inLib.count("test") && !inLib.count("Mover")); // only Lib's annotations follow `Lib::`

		const std::string typed = std::string(kDeclared) + "[Lib::Co]\nstruct Fresh { public int32 v; }\n";
		std::set<std::string> prefixed = Labels(CompleteAt(typed, "[Lib::Co"));
		TEST_ASSERT(prefixed.size() == 1 && prefixed.count("Component"));
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestNoAnnotationsInAnIndex()
	{
		std::set<std::string> labels = Labels(CompleteAt(kDeclared, "return values["));
		TEST_ASSERT(!labels.count("Requires") && !labels.count("Range") && !labels.count("test"));
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestSignatureHelpShowsEveryOverload()
	{
		const std::string text = std::string(kDeclared) + "namespace Lib { [Requires(Mover, \nstruct Fresh { public int32 v; } }\n";
		auto session = Testing::Lsp::MakeSession(kFile, text);
		auto help = SignatureHelp::At(session, Testing::Lsp::UriFor(kFile), After(text, "[Requires(Mover, "));
		TEST_ASSERT(help.found);
		TEST_ASSERT_EQ(help.signatures.Count(), 2u);
		TEST_ASSERT(help.signatures[0].label == "Requires(typename component, typename... more)");
		TEST_ASSERT(help.signatures[1].label == "Requires(const char* typeName, const char*... more)");
		TEST_ASSERT_EQ(help.signatures[0].parameters.Count(), 2u);
		TEST_ASSERT(help.signatures[0].parameters[1].label == "typename... more");
		TEST_ASSERT_EQ(help.activeParameter, 1u);

		const std::string range = std::string(kDeclared) + "struct Fresh { [Lib::Range(0, ] public float v; }\n";
		auto rangeSession = Testing::Lsp::MakeSession(kFile, range);
		auto rangeHelp = SignatureHelp::At(rangeSession, Testing::Lsp::UriFor(kFile), After(range, "[Lib::Range(0, "));
		TEST_ASSERT(rangeHelp.found && rangeHelp.signatures.Count() == 1 && rangeHelp.activeParameter == 1);
		TEST_ASSERT(rangeHelp.signatures[0].label == "Range(double min, double max)");
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestAMisspeltAnnotationIsADiagnostic()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kTypo);
		const auto diagnostics = Diagnostics::ForDocument(session, Testing::Lsp::UriFor(kFile));
		TEST_ASSERT_EQ(diagnostics.Count(), (uint32)1);
		TEST_ASSERT(Testing::Lsp::Contains(diagnostics[0].message, "unknown annotation 'Requiers': did you mean 'Requires'"));
		TEST_ASSERT(SamePlace(diagnostics[0].range.start, Testing::Lsp::PositionOf(kTypo, "Requiers")));
		TEST_PASSED();
	}

	void LspDeclaredAnnotationsTest::TestAWrongArgumentIsADiagnosticWhereItIs()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kWrong);
		const auto diagnostics = Diagnostics::ForDocument(session, Testing::Lsp::UriFor(kFile));
		TEST_ASSERT_EQ(diagnostics.Count(), (uint32)1);
		TEST_ASSERT(Testing::Lsp::Contains(diagnostics[0].message, "[Range] argument 2 ('max') is a double: \"far\" is a string"));
		TEST_ASSERT(SamePlace(diagnostics[0].range.start, Testing::Lsp::PositionOf(kWrong, "\"far\"")));
		TEST_PASSED();
	}
} // namespace Testing
