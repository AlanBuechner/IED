#include "LspCompletionTest.h"
#include "LspTestSupport.h"
#include "Features/Completion.h"
#include "Features/MemberChain.h"
#include "Server/PlaceInAnalysis.h"
#include <algorithm>
#include <set>
#include <string>

using C4LSP::Features::Completion;
using C4LSP::Features::CompletionItem;
using C4LSP::Features::MemberChain;
using C4LSP::Protocol::Position;
using C4LSP::Server::PlaceInAnalyzedText;

namespace Testing
{
	namespace
	{
		// A script's shape: a component reaches its transform through a reference whose `->` is an operator, from a method
		// defined out of line in a type deriving from the one that declares GetTransform.
		const char* kGame = R"(namespace Game
{
	struct Vec { public float x; public float y; public float Length() const { return x; } };

	struct Transform
	{
		public Vec m_Position;
		public void Rotate(float angle) {}
		public void Translate(Vec by) {}
	};

	template<T> struct Ref
	{
		[operator("->")] public T* Arrow() const { return m_Target; }
		public T* m_Target;
	};

	struct Shape { public float Radius; public Vec Center; };
	struct Body { public Shape Main; public Shape[4] Parts; };

	struct Behaviour
	{
		public Ref<Transform> GetTransform() const { return m_Transform; }
		public float GetDeltaTime() const { return 0.0f; }
		public Ref<Transform> m_Transform;
	};

	struct Player : Behaviour
	{
		public float m_Speed;
		public Body m_Body;
		public Transform* m_Raw;
		public void OnUpdate();
	};

	public void Player::OnUpdate()
	{
		GetTransform()->Rotate(m_Speed * GetDeltaTime());
		m_Body.Main.Radius = 1.0f;
		m_Body.Parts[0].Center.x = 2.0f;
		m_Raw->Translate(m_Body.Main.Center);
	}
}

int32 main() { return 0; }
)";

		const char* kFile = "Game.cf";

		// The place just after the `occurrence`-th `needle` in `text`.
		Position After(const std::string& text, const std::string& needle, int occurrence = 1)
		{
			Position at = Testing::Lsp::PositionOf(text, needle, occurrence);
			at.character += static_cast<uint32>(needle.size());
			return at;
		}

		std::set<std::string> Labels(const Core::Vector<CompletionItem>& items)
		{
			std::set<std::string> labels;
			for (const CompletionItem& item : items) labels.insert(item.label);
			return labels;
		}

		bool Has(const std::set<std::string>& labels, const std::string& label) { return labels.count(label) > 0; }

		// The links of the chain before the end of `text`, written `a(.)b[]->c()`: each name, its suffixes, its access.
		std::string ChainOf(const std::string& text)
		{
			const std::optional<MemberChain> chain = MemberChain::Before(text, text.size());
			if (!chain) return "none";
			std::string written;
			for (const MemberChain::Link& link : chain->Links())
			{
				written += link.name;
				for (MemberChain::Suffix suffix : link.suffixes) written += suffix == MemberChain::Suffix::Call ? "()" : "[]";
				written += link.next == MemberChain::Access::Dot ? "." : link.next == MemberChain::Access::Arrow ? "->" : "::";
			}
			return written;
		}

		std::string Analyzed(const std::string& analyzed, const std::string& now, Position position)
		{
			const auto place = PlaceInAnalyzedText(analyzed, now, position);
			return std::to_string(place.position.line) + ":" + std::to_string(place.position.character) + (place.edited ? " edited" : "");
		}
	}

	// --- The place in the analyzed text ------------------------------------------------------------------------------------

	void LspCompletionTest::TestAPlaceBeforeOrAfterTheEditsIsTheSamePlace()
	{
		const std::string analyzed = "void F()\n{\n\tint32 a = 1;\n}\n";
		const std::string now = "void F()\n{\n\tint32 a = 1;\n\ta.\n}\n";
		TEST_ASSERT_EQ(Analyzed(analyzed, now, { 2, 3 }), std::string("2:3"));     // before the typed line
		TEST_ASSERT_EQ(Analyzed(analyzed, now, { 4, 1 }), std::string("3:1"));     // the `}` after it, a line up in the analyzed text
		TEST_ASSERT_EQ(Analyzed(analyzed, analyzed, { 3, 0 }), std::string("3:0")); // nothing typed
		TEST_PASSED();
	}

	void LspCompletionTest::TestAPlaceInsideTheEditsIsWhereTheyStart()
	{
		const std::string analyzed = "void F()\n{\n\tint32 a = 1;\n}\n";
		const std::string now = "void F()\n{\n\tint32 a = 1;\n\tGetTransform()->Tr\n}\n";
		// Inside the function still: where the typed line begins, the end of the line before it.
		TEST_ASSERT_EQ(Analyzed(analyzed, now, { 3, 10 }), std::string("3:0 edited"));
		TEST_ASSERT_EQ(Analyzed(analyzed, now, { 3, 19 }), std::string("3:0 edited"));
		const std::string renamed = "void F()\n{\n\tint32 abc = 1;\n}\n";
		TEST_ASSERT_EQ(Analyzed(analyzed, renamed, { 2, 9 }), std::string("2:8 edited"));
		TEST_PASSED();
	}

	void LspCompletionTest::TestAPlaceAfterADeletionMovesBack()
	{
		const std::string analyzed = "int32 a;\nint32 bbbb;\nint32 c;\n";
		const std::string now = "int32 a;\nint32 c;\n";
		TEST_ASSERT_EQ(Analyzed(analyzed, now, { 1, 8 }), std::string("2:8")); // after what was deleted: as far back
		TEST_ASSERT_EQ(Analyzed(analyzed, now, { 0, 3 }), std::string("0:3"));
		TEST_ASSERT_EQ(Analyzed(analyzed, "", { 0, 0 }), std::string("0:0"));
		TEST_PASSED();
	}

	// --- The chain a member access is made on --------------------------------------------------------------------------

	void LspCompletionTest::TestAChainReadsCallsIndexesAndMembers()
	{
		TEST_ASSERT_EQ(ChainOf("\tGetTransform()->"), std::string("GetTransform()->"));
		TEST_ASSERT_EQ(ChainOf("x = m_Body.Main.Ra"), std::string("m_Body.Main."));
		TEST_ASSERT_EQ(ChainOf("m_Body.Parts[i + 1].Center.x"), std::string("m_Body.Parts[].Center."));
		TEST_ASSERT_EQ(ChainOf("Get(a->b, (c)).Make<Mesh>()-> "), std::string("Get().Make()->"));
		TEST_ASSERT_EQ(ChainOf("Log(\")(\").m"), std::string("Log()."));
		TEST_ASSERT_EQ(ChainOf("a\n\t\t.b\n\t\t."), std::string("a.b."));
		TEST_ASSERT_EQ(ChainOf("d.\n\tv."), std::string("v.")); // a name beginning a line starts a chain of its own
		TEST_ASSERT_EQ(ChainOf("this->m_"), std::string("this->"));
		TEST_PASSED();
	}

	void LspCompletionTest::TestAChainReadsAQualifiedStart()
	{
		TEST_ASSERT_EQ(ChainOf("Math::FVector3::Up."), std::string("Math::FVector3::Up."));
		TEST_ASSERT_EQ(ChainOf("Scene::World::Get()->"), std::string("Scene::World::Get()->"));
		TEST_PASSED();
	}

	void LspCompletionTest::TestNoChainWhereNoValueIsAccessed()
	{
		TEST_ASSERT_EQ(ChainOf("(x + y)."), std::string("none"));
		TEST_ASSERT_EQ(ChainOf("float f = 1."), std::string("none"));
		TEST_ASSERT_EQ(ChainOf("Render::Pa"), std::string("none")); // a qualifier: no value is accessed
		TEST_ASSERT_EQ(ChainOf("a - b"), std::string("none"));
		TEST_ASSERT_EQ(ChainOf("x)."), std::string("none"));   // its call opens before the statement
		TEST_ASSERT_EQ(ChainOf(""), std::string("none"));
		TEST_PASSED();
	}

	// --- What completion offers -----------------------------------------------------------------------------------------

	void LspCompletionTest::TestMembersOfWhatACallReturns()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kGame);
		const std::string uri = Testing::Lsp::UriFor(kFile);
		const auto labels = Labels(Completion::At(session, uri, After(kGame, "GetTransform()->")));
		TEST_ASSERT(Has(labels, "Rotate"));
		TEST_ASSERT(Has(labels, "Translate"));
		TEST_ASSERT(Has(labels, "m_Position"));
		TEST_ASSERT(!Has(labels, "Arrow"));  // the reference's own members are not the transform's
		TEST_ASSERT(!Has(labels, "Player")); // after an accessor, no global fits
		const auto typed = Labels(Completion::At(session, uri, After(kGame, "m_Raw->Tr")));
		TEST_ASSERT_EQ(typed.size(), size_t(1));
		TEST_ASSERT(Has(typed, "Translate"));
		TEST_PASSED();
	}

	void LspCompletionTest::TestMembersThroughFieldsAndIndexes()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kGame);
		const std::string uri = Testing::Lsp::UriFor(kFile);
		const auto shape = Labels(Completion::At(session, uri, After(kGame, "m_Body.Main.")));
		TEST_ASSERT(Has(shape, "Radius"));
		TEST_ASSERT(Has(shape, "Center"));
		TEST_ASSERT(!Has(shape, "Parts"));
		const auto vec = Labels(Completion::At(session, uri, After(kGame, "m_Body.Parts[0].Center.")));
		TEST_ASSERT(Has(vec, "x"));
		TEST_ASSERT(Has(vec, "Length"));
		TEST_ASSERT(!Has(vec, "Radius"));
		TEST_PASSED();
	}

	void LspCompletionTest::TestMembersThroughAnArrowOperator()
	{
		const std::string text = std::string(kGame) + "void Use(Game::Ref<Game::Transform> held) { held->Rotate(1.0f); }\n";
		auto session = Testing::Lsp::MakeSession(kFile, text);
		const auto labels = Labels(Completion::At(session, Testing::Lsp::UriFor(kFile), After(text, "held->")));
		TEST_ASSERT(Has(labels, "Rotate"));
		TEST_ASSERT(!Has(labels, "m_Target"));
		TEST_PASSED();
	}

	void LspCompletionTest::TestAnOutOfLineMethodOffersWhatThisReaches()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kGame);
		const std::string uri = Testing::Lsp::UriFor(kFile);
		const auto get = Labels(Completion::At(session, uri, After(kGame, "\t\tGet")));
		TEST_ASSERT(Has(get, "GetTransform")); // a base's method, through an implicit `this`
		TEST_ASSERT(Has(get, "GetDeltaTime"));
		const auto own = Labels(Completion::At(session, uri, After(kGame, "\t\tm_")));
		TEST_ASSERT(Has(own, "m_Speed"));
		TEST_ASSERT(Has(own, "m_Body"));
		TEST_ASSERT(Has(own, "m_Transform")); // inherited
		TEST_PASSED();
	}

	void LspCompletionTest::TestTextTypedSinceTheAnalysisCompletes()
	{
		auto session = Testing::Lsp::MakeSession(kFile, kGame);
		const std::string uri = Testing::Lsp::UriFor(kFile);
		const std::string anchor = "\t\tm_Raw->Translate(m_Body.Main.Center);\n";
		std::string now = kGame;
		now.insert(now.find(anchor) + anchor.size(), "\t\tGetTransform()->Tr");
		const auto members = Labels(Completion::At(session, uri, now, After(now, "GetTransform()->Tr")));
		TEST_ASSERT(Has(members, "Translate"));
		TEST_ASSERT(!Has(members, "Rotate"));
		std::string named = kGame;
		named.insert(named.find(anchor) + anchor.size(), "\t\tm_Sp");
		const auto own = Labels(Completion::At(session, uri, named, After(named, "\t\tm_Sp")));
		TEST_ASSERT(Has(own, "m_Speed")); // the method around the new line is the analysis's
		TEST_PASSED();
	}

	void LspCompletionTest::TestAGlobalIsOfferedOnce()
	{
		const std::string text = "int32 Twice(int32 a) { return a * 2; }\nint32 Twice(float a) { return 2; }\nint32 main() { return Tw; }\n";
		auto session = Testing::Lsp::MakeSession(kFile, text);
		const auto items = Completion::At(session, Testing::Lsp::UriFor(kFile), After(text, "return Tw"));
		TEST_ASSERT_EQ(std::count_if(items.begin(), items.end(), [](const CompletionItem& item) { return item.label == "Twice"; }), std::ptrdiff_t(1));
		TEST_PASSED();
	}
} // namespace Testing
