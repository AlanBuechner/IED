#include "LspConstructionsTest.h"
#include "LspTestSupport.h"
#include "Features/CallHierarchy.h"
#include "Features/Definition.h"
#include "Features/References.h"
#include "Features/Rename.h"
#include "Compiler/FileIo.h"
#include <filesystem>
#include <process.h>

using namespace C4LSP::Features;

namespace Testing
{
	namespace
	{
		const char* const kPoolHeader =
			R"(class Server;
class Pool
{
	public Server* server;
	[operator("create")] public void Init(Server* s);
	public int32 Serving();
}
)";

		const char* const kPoolCode =
			R"(include "pool.ch"

void Pool::Init(Server* s) {
	server = s;
	Pool* self = this;
}

int32 Pool::Serving() { return 1; }
)";

		const char* const kServerHeader =
			R"(include "pool.ch"

class Server
{
	public Pool* pool;
	public void Start();
}
)";

		const char* const kServerCode =
			R"(include "server.ch"

void Server::Start() {
	pool = new Pool(this);
	Pool local(this);
	int32 n = Pool(this).Serving();
}
)";

		C4LSP::Server::CompilationSession PoolSession()
		{
			C4LSP::Server::CompilationSession session;
			session.RegisterInMemorySource("pool.ch", kPoolHeader);
			session.RegisterInMemorySource("pool.cf", kPoolCode);
			session.RegisterInMemorySource("server.ch", kServerHeader);
			session.RegisterInMemorySource("server.cf", kServerCode);
			session.Recompile();
			return session;
		}

		int CountIn(const Core::Vector<C4LSP::Protocol::Location>& locations, const std::string& file)
		{
			int count = 0;
			for (const auto& location : locations)
				if (location.uri == Testing::Lsp::UriFor(file)) ++count;
			return count;
		}

		bool Has(const Core::Vector<C4LSP::Protocol::Location>& locations, const std::string& file, const C4LSP::Protocol::Position& at)
		{
			for (const auto& location : locations)
				if (location.uri == Testing::Lsp::UriFor(file) && location.range.start.line == at.line && location.range.start.character == at.character)
					return true;
			return false;
		}
	}

	void LspConstructionsTest::TestReferencesOfAClassIncludeEveryWrittenUse()
	{
		auto session = PoolSession();
		auto uses = References::At(session, Testing::Lsp::UriFor("pool.ch"), Testing::Lsp::PositionOf(kPoolHeader, "Pool"));
		TEST_ASSERT(Has(uses, "pool.cf", Testing::Lsp::PositionOf(kPoolCode, "Pool::Init")));     // a definition's qualifier
		TEST_ASSERT(Has(uses, "pool.cf", Testing::Lsp::PositionOf(kPoolCode, "Pool::Serving")));
		TEST_ASSERT(Has(uses, "pool.cf", Testing::Lsp::PositionOf(kPoolCode, "Pool* self")));
		TEST_ASSERT(Has(uses, "server.ch", Testing::Lsp::PositionOf(kServerHeader, "Pool* pool"))); // a field in another header
		TEST_ASSERT(Has(uses, "server.cf", Testing::Lsp::PositionOf(kServerCode, "Pool(this);")));  // `new Pool(this)`
		TEST_ASSERT(Has(uses, "server.cf", Testing::Lsp::PositionOf(kServerCode, "Pool local")));
		TEST_ASSERT(Has(uses, "server.cf", Testing::Lsp::PositionOf(kServerCode, "Pool(this).")));  // a temporary
		TEST_ASSERT_EQ(uses.Count(), 7u);
		TEST_PASSED();
	}

	void LspConstructionsTest::TestReferencesOfAConstructorAreItsConstructions()
	{
		auto session = PoolSession();
		const auto init = Testing::Lsp::PositionOf(kPoolHeader, "Init");
		auto calls = References::At(session, Testing::Lsp::UriFor("pool.ch"), init);
		TEST_ASSERT_EQ(calls.Count(), 3u);
		TEST_ASSERT(Has(calls, "server.cf", Testing::Lsp::PositionOf(kServerCode, "Pool(this);")));
		TEST_ASSERT(Has(calls, "server.cf", Testing::Lsp::PositionOf(kServerCode, "local")));
		TEST_ASSERT(Has(calls, "server.cf", Testing::Lsp::PositionOf(kServerCode, "Pool(this).")));
		// From the definition too, with both declarations.
		auto all = References::At(session, Testing::Lsp::UriFor("pool.cf"), Testing::Lsp::PositionOf(kPoolCode, "Init"), true);
		TEST_ASSERT_EQ(all.Count(), 5u);
		TEST_ASSERT_EQ(CountIn(all, "pool.ch"), 1);
		TEST_ASSERT_EQ(CountIn(all, "pool.cf"), 1);
		TEST_ASSERT_EQ(CountIn(all, "server.cf"), 3);
		TEST_PASSED();
	}

	void LspConstructionsTest::TestRenameKeepsConstructionsAndRenamesQualifiers()
	{
		auto session = PoolSession();
		RenameResult init = Rename::At(session, Testing::Lsp::UriFor("pool.ch"), Testing::Lsp::PositionOf(kPoolHeader, "Init"));
		TEST_ASSERT(init.found);
		TEST_ASSERT_EQ(init.occurrences.Count(), 2u); // its declarations; a construction spells the type's name
		RenameResult pool = Rename::At(session, Testing::Lsp::UriFor("pool.ch"), Testing::Lsp::PositionOf(kPoolHeader, "Pool"));
		TEST_ASSERT(pool.found);
		TEST_ASSERT_EQ(pool.occurrences.Count(), 8u); // the class's name and its seven uses, qualifiers included
		TEST_PASSED();
	}

	void LspConstructionsTest::TestConstructionsAreCallsOfTheConstructor()
	{
		auto session = PoolSession();
		auto prepared = CallHierarchy::Prepare(session, Testing::Lsp::UriFor("pool.ch"), Testing::Lsp::PositionOf(kPoolHeader, "Init"));
		TEST_ASSERT_EQ(prepared.Count(), 1u);
		auto incoming = CallHierarchy::Incoming(session, prepared[0]);
		TEST_ASSERT_EQ(incoming.Count(), 1u);
		TEST_ASSERT_EQ(incoming[0].item.name, std::string("Start"));
		TEST_ASSERT_EQ(incoming[0].fromRanges.Count(), 3u);
		auto start = CallHierarchy::Prepare(session, Testing::Lsp::UriFor("server.cf"), Testing::Lsp::PositionOf(kServerCode, "Start"));
		TEST_ASSERT_EQ(start.Count(), 1u);
		auto outgoing = CallHierarchy::Outgoing(session, start[0]);
		int initRanges = 0;
		for (const HierarchyCall& call : outgoing)
			if (call.item.name == "Init") initRanges += (int)call.fromRanges.Count();
		TEST_ASSERT_EQ(initRanges, 3);
		TEST_PASSED();
	}

	void LspConstructionsTest::TestTheTypeOfANewGoesToItsClass()
	{
		// `T` of `new T(args)` is a node of the tree (it was not a child of the new-expression, so no walk reached it).
		auto session = PoolSession();
		auto target = Definition::At(session, Testing::Lsp::UriFor("server.cf"), Testing::Lsp::PositionOf(kServerCode, "Pool(this);"));
		TEST_ASSERT_EQ(target.Count(), 1u);
		TEST_ASSERT_EQ(target[0].uri, Testing::Lsp::UriFor("pool.ch"));
		TEST_ASSERT_EQ(target[0].range.start.line, Testing::Lsp::PositionOf(kPoolHeader, "Pool").line);
		TEST_PASSED();
	}

	void LspConstructionsTest::TestATypeWrittenInADeclarationIsItsClass()
	{
		// From `Pool` of the field `Pool* pool`: the class's references, and a rename of it from there.
		auto session = PoolSession();
		const auto field = Testing::Lsp::PositionOf(kServerHeader, "Pool* pool");
		auto uses = References::At(session, Testing::Lsp::UriFor("server.ch"), field);
		TEST_ASSERT_EQ(uses.Count(), 7u);
		TEST_ASSERT(Has(uses, "server.ch", field));
		PrepareResult prepared = Rename::PrepareAt(session, Testing::Lsp::UriFor("server.ch"), field);
		TEST_ASSERT(prepared.found);
		TEST_ASSERT_EQ(prepared.placeholder, std::string("Pool"));
		TEST_PASSED();
	}

	void LspConstructionsTest::TestAProjectOnDiskFindsTheFieldInAnotherHeader()
	{
		// As IED's server is: files in folders, a build description, includes by the project's paths, a namespace.
		namespace fs = std::filesystem;
		const fs::path root = fs::temp_directory_path() / ("c4_lsp_constructions_" + std::to_string(_getpid()));
		const fs::path src = root / "src";
		const auto write = [&](const char* name, const std::string& text) {
			Scripting::CreateFolders(Scripting::PathText((src / name).parent_path()));
			TEST_ASSERT(Scripting::WriteFileBytes(Scripting::PathText(src / name), text));
		};
		write("lsp/pool.ch", "namespace Ied { class Server; class Pool { public Server* server; [operator(\"create\")] public void Init(Server* s); } }\n");
		write("lsp/pool.cf", "include \"pool.ch\"\ninclude \"host/server.ch\"\nnamespace Ied { void Pool::Init(Server* s) { server = s; } }\n");
		write("host/server.ch", "include \"lsp/pool.ch\"\nnamespace Ied { class Server { public Pool* pool; public void Start(); } }\n");
		write("host/server.cf", "include \"server.ch\"\nnamespace Ied { void Server::Start() { pool = new Pool(this); } }\nint32 main() { return 0; }\n");
		const std::string projects = Scripting::PathText(root / "projects.toml");
		std::string folder = Scripting::PathText(src);
		for (char& c : folder) if (c == '\\') c = '/';
		TEST_ASSERT(Scripting::WriteFileBytes(projects, "root = \"app\"\n[[project]]\nname = \"app\"\nsources = [\"" + folder + "\"]\n"));
		C4LSP::Server::CompilationSession session;
		session.SetProjectsFile(projects);
		const std::string poolUri = C4LSP::Protocol::DocumentUri::ToUri(Scripting::PathText(src / "lsp/pool.ch"));
		const std::string poolText = Scripting::ReadFileBytes(Scripting::PathText(src / "lsp/pool.ch"));
		session.SetOpenDocument(poolUri, poolText);
		session.Recompile();
		auto uses = References::At(session, poolUri, Testing::Lsp::PositionOf(poolText, "Pool {"));
		int inServerHeader = 0;
		for (const auto& location : uses)
			if (location.uri.find("server.ch") != std::string::npos) ++inServerHeader;
		TEST_ASSERT_EQ(inServerHeader, 1); // `Pool* pool`
		TEST_ASSERT_EQ(uses.Count(), 3u);  // and `Pool::Init`, `new Pool`
		std::error_code ec;
		fs::remove_all(Scripting::ExtendedPath(root), ec);
		TEST_PASSED();
	}
} // namespace Testing
