#include "LspProgramsTest.h"
#include "LspServerHarness.h"
#include "LspTestSupport.h"
#include "Server/ProgramPlan.h"
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

using Json::Value;
using Testing::Lsp::Contains;
using Testing::Lsp::FileUri;
using Testing::Lsp::ServerHarness;
using Testing::Lsp::WriteFile;

namespace Testing
{
	namespace
	{
		// An engine module, a game module on it, a client program with the game, and a tool program with the engine alone.
		const char* kEngineHeader =
			"namespace Engine\n{\n\tinterface IModule\n\t{\n\t\tpublic virtual int32 Run() = 0;\n\t}\n\n\tint32 EngineValue();\n}\n";
		const char* kEngineSource = "include \"Runtime/module.ch\"\n\nnamespace Engine\n{\n\tint32 EngineValue() { return 7; }\n}\n";
		const char* kGameModule =
			"include \"Runtime/module.ch\"\n\nnamespace Game\n{\n\tclass Module : Engine::IModule\n\t{\n\t\tpublic override int32 Run() { return Engine::EngineValue(); }\n\t}\n}\n";
		const char* kClientMain = "include \"Game/game_module.ch\"\n\nint32 main()\n{\n\tGame::Module module;\n\treturn module.Run();\n}\n";
		const char* kToolMain =
			"include \"Runtime/module.ch\"\n\nnamespace Tool\n{\n\tclass Module : Engine::IModule\n\t{\n\t\tpublic override int32 Run() { return Engine::EngineValue() + 1; }\n\t}\n}\n\n"
			"int32 main()\n{\n\tTool::Module module;\n\treturn module.Run();\n}\n";

		struct Workspace
		{
			fs::path root;
			std::string engineHeader, engineSource, gameModule, clientMain, toolMain; // URIs

			// {Client: {projects}, Tool: {projects}}: the programs as IED hands over Nuke's options files.
			Value Programs() const
			{
				Value programs = Value::MakeObject();
				for (const char* name : { "Client", "Tool" })
				{
					Value options = Value::MakeObject();
					options.SetMember("projects", Value::String(Core::String((root / "build" / name / "projects.toml").generic_string())));
					programs.SetMember(name, options);
				}
				return programs;
			}

			Value WithPrograms() const
			{
				Value options = Value::MakeObject();
				options.SetMember("programs", Programs());
				return options;
			}
		};

		Workspace MakeWorkspace(const std::string& name)
		{
			Workspace ws;
			ws.root = fs::temp_directory_path() / name;
			fs::remove_all(ws.root);
			WriteFile(ws.root / "Engine/src/Runtime/module.ch", kEngineHeader);
			WriteFile(ws.root / "Engine/src/Runtime/module.cf", kEngineSource);
			WriteFile(ws.root / "Game/src/Game/game_module.ch", kGameModule);
			WriteFile(ws.root / "Client/src/main.cf", kClientMain);
			WriteFile(ws.root / "Tool/src/main.cf", kToolMain);
			const std::string engine = "[[project]]\nname = \"Engine\"\nsources = [\"" + (ws.root / "Engine/src").generic_string() + "\"]\n";
			const std::string game = "[[project]]\nname = \"Game\"\nsources = [\"" + (ws.root / "Game/src").generic_string() + "\"]\ndepends = [\"Engine\"]\n";
			WriteFile(ws.root / "build/Client/projects.toml", "root = \"Client\"\n[[project]]\nname = \"Client\"\nsources = [\"" + (ws.root / "Client/src").generic_string() +
				"\"]\ndepends = [\"Game\"]\n" + game + engine);
			WriteFile(ws.root / "build/Tool/projects.toml", "root = \"Tool\"\n[[project]]\nname = \"Tool\"\nsources = [\"" + (ws.root / "Tool/src").generic_string() +
				"\"]\ndepends = [\"Engine\"]\n" + engine);
			ws.engineHeader = FileUri(ws.root / "Engine/src/Runtime/module.ch");
			ws.engineSource = FileUri(ws.root / "Engine/src/Runtime/module.cf");
			ws.gameModule = FileUri(ws.root / "Game/src/Game/game_module.ch");
			ws.clientMain = FileUri(ws.root / "Client/src/main.cf");
			ws.toolMain = FileUri(ws.root / "Tool/src/main.cf");
			return ws;
		}

		// The URIs a list of locations (or of objects with a `uri`, or with an item's) names, as one text.
		std::string UrisOf(const Value& list, const char* member = nullptr)
		{
			std::string uris;
			for (uint32 i = 0; i < list.Size(); ++i)
			{
				const Value entry = member ? list.At(i).Member(member) : list.At(i);
				uris += std::string(entry.Member("uri").GetString()) + "\n";
			}
			return uris;
		}

		bool Names(const std::string& uris, const std::string& uri)
		{
			auto lower = [](std::string text) { for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return text; };
			return Contains(lower(uris), lower(uri));
		}
	}

	void LspProgramsTest::TestAModuleIsAnalyzedInAProgramThatCompilesIt()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_module");
		{
			ServerHarness alone(FileUri(ws.root)); // IED's server for the module's project: no options of its own
			alone.Initialize(Value::MakeObject());
			alone.Open(ws.gameModule, kGameModule);
			TEST_ASSERT(Contains(alone.MessagesFor(ws.gameModule), "include not found: 'Runtime/module.ch'"));
		}
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.gameModule, kGameModule);
		TEST_ASSERT_EQ(server.MessagesFor(ws.gameModule), std::string());
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestOneAnalysisServesTheDocumentsOfEveryProjectItHolds()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_cover");
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		TEST_ASSERT_EQ(server.Server().AnalyzedProgramCount(), size_t(0)); // nothing open, nothing analyzed
		server.Open(ws.engineHeader, kEngineHeader);
		server.Open(ws.gameModule, kGameModule);
		server.Open(ws.clientMain, kClientMain);
		TEST_ASSERT_EQ(server.Server().AnalyzedProgramCount(), size_t(1)); // the client's program holds all three
		for (const std::string& uri : { ws.engineHeader, ws.gameModule, ws.clientMain }) TEST_ASSERT_EQ(server.MessagesFor(uri), std::string());
		server.Open(ws.toolMain, kToolMain);
		TEST_ASSERT_EQ(server.Server().AnalyzedProgramCount(), size_t(2));
		TEST_ASSERT_EQ(server.MessagesFor(ws.toolMain), std::string());
		server.Close(ws.toolMain);
		TEST_ASSERT_EQ(server.Server().AnalyzedProgramCount(), size_t(1)); // the tool's analysis goes with its last document
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestReferencesReachEveryProgram()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_references");
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.engineHeader, kEngineHeader);
		Value params = ServerHarness::At(ws.engineHeader, kEngineHeader, "EngineValue();");
		Value context = Value::MakeObject();
		context.SetMember("includeDeclaration", Value::Bool(true));
		params.SetMember("context", context);
		const std::string uris = UrisOf(server.Request("textDocument/references", params));
		TEST_ASSERT(Names(uris, ws.engineHeader));
		TEST_ASSERT(Names(uris, ws.gameModule)); // the client's program
		TEST_ASSERT(Names(uris, ws.toolMain));   // the tool's
		TEST_ASSERT_EQ(server.Server().AnalyzedProgramCount(), size_t(2)); // the one asked kept a while

		// Asked at a use in a file only the tool compiles: the client's program is asked at the declaration.
		server.Open(ws.toolMain, kToolMain);
		Value atUse = ServerHarness::At(ws.toolMain, kToolMain, "EngineValue() + 1");
		atUse.SetMember("context", context);
		const std::string fromUse = UrisOf(server.Request("textDocument/references", atUse));
		TEST_ASSERT(Names(fromUse, ws.gameModule));
		TEST_ASSERT(Names(fromUse, ws.engineSource));
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestImplementationsReachEveryProgram()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_implementations");
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.engineHeader, kEngineHeader);
		const Value found = server.Request("textDocument/implementation", ServerHarness::At(ws.engineHeader, kEngineHeader, "Run() = 0"));
		TEST_ASSERT_EQ(found.Size(), 2u);
		const std::string uris = UrisOf(found);
		TEST_ASSERT(Names(uris, ws.gameModule));
		TEST_ASSERT(Names(uris, ws.toolMain));
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestIncomingCallsAndSubtypesReachEveryProgram()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_hierarchy");
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.engineHeader, kEngineHeader);
		const Value functions = server.Request("textDocument/prepareCallHierarchy", ServerHarness::At(ws.engineHeader, kEngineHeader, "EngineValue();"));
		TEST_ASSERT_EQ(functions.Size(), 1u);
		Value item = Value::MakeObject();
		item.SetMember("item", functions.At(0));
		const std::string callers = UrisOf(server.Request("callHierarchy/incomingCalls", item), "from");
		TEST_ASSERT(Names(callers, ws.gameModule));
		TEST_ASSERT(Names(callers, ws.toolMain));

		const Value types = server.Request("textDocument/prepareTypeHierarchy", ServerHarness::At(ws.engineHeader, kEngineHeader, "IModule"));
		TEST_ASSERT_EQ(types.Size(), 1u);
		Value type = Value::MakeObject();
		type.SetMember("item", types.At(0));
		const std::string subtypes = UrisOf(server.Request("typeHierarchy/subtypes", type));
		TEST_ASSERT(Names(subtypes, ws.gameModule));
		TEST_ASSERT(Names(subtypes, ws.toolMain));
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestRenameReachesEveryProgram()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_rename");
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.engineHeader, kEngineHeader);
		Value params = ServerHarness::At(ws.engineHeader, kEngineHeader, "EngineValue();");
		params.SetMember("newName", Value::String(Core::String("CoreValue")));
		const Value edit = server.Request("textDocument/rename", params);
		const std::string changed = edit.Serialize();
		TEST_ASSERT(Names(changed, ws.gameModule));
		TEST_ASSERT(Names(changed, ws.toolMain));
		TEST_ASSERT(Names(changed, ws.engineSource));
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestWorkspaceSymbolsOfEveryProgram()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_symbols");
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		Value query = Value::MakeObject();
		query.SetMember("query", Value::String(Core::String("Module")));
		const Value symbols = server.Request("workspace/symbol", query);
		std::string containers;
		for (uint32 i = 0; i < symbols.Size(); ++i)
			containers += std::string(symbols.At(i).Member("containerName").GetString()) + "::" + std::string(symbols.At(i).Member("name").GetString()) + "\n";
		TEST_ASSERT(Contains(containers, "Game::Module"));
		TEST_ASSERT(Contains(containers, "Tool::Module"));
		TEST_ASSERT(Contains(containers, "Engine::IModule"));
		TEST_ASSERT_EQ(containers.find("Engine::IModule"), containers.rfind("Engine::IModule")); // one Engine, though both programs hold it
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestProgramsArriveWithAConfigurationChange()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_configuration");
		ServerHarness server(FileUri(ws.root));
		server.Initialize(Value::MakeObject());
		server.Open(ws.gameModule, kGameModule);
		TEST_ASSERT(Contains(server.MessagesFor(ws.gameModule), "include not found"));
		Value settings = Value::MakeObject();
		settings.SetMember("programs", ws.Programs());
		Value params = Value::MakeObject();
		params.SetMember("settings", settings);
		server.Notify("workspace/didChangeConfiguration", params);
		TEST_ASSERT_EQ(server.MessagesFor(ws.gameModule), std::string());
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestMalformedProgramsAreLeftOut()
	{
		const Workspace ws = MakeWorkspace("c4lsp_programs_malformed");
		ServerHarness server(FileUri(ws.root));
		Value options = ws.WithPrograms();
		Value programs = options.Member("programs");
		programs.SetMember("Broken", Value::String(Core::String("not an object")));
		Value missing = Value::MakeObject();
		missing.SetMember("projects", Value::String(Core::String((ws.root / "nowhere.toml").generic_string())));
		programs.SetMember("Missing", missing);
		options.SetMember("programs", programs);
		server.Initialize(options);
		server.Open(ws.gameModule, kGameModule);
		TEST_ASSERT_EQ(server.MessagesFor(ws.gameModule), std::string()); // the good programs still serve
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspProgramsTest::TestAssignDocumentsCoversWithFewPrograms()
	{
		using C4LSP::Server::AssignDocuments;
		using C4LSP::Server::ProgramFacts;
		// Documents 0..3; the small program holds 0, the big one 0..2; nothing holds 3.
		const ProgramFacts small{ { true, false, false, false }, false, 1 };
		const ProgramFacts big{ { true, true, true, false }, false, 5 };
		TEST_ASSERT(AssignDocuments({ small, big }, 4) == std::vector<int32>({ 1, 1, 1, -1 }));
		// Holding as many, the analyzed one wins, then the one with fewer projects.
		const ProgramFacts analyzedBig{ { true, false, false, false }, true, 5 };
		TEST_ASSERT(AssignDocuments({ small, analyzedBig }, 1) == std::vector<int32>({ 1 }));
		TEST_ASSERT(AssignDocuments({ big, small }, 1) == std::vector<int32>({ 1 }));
		TEST_ASSERT(AssignDocuments({}, 2) == std::vector<int32>({ -1, -1 }));
		TEST_ASSERT(AssignDocuments({ small }, 0).empty());
		TEST_PASSED();
	}

	void LspProgramsTest::TestProgramsToConsultReachWhatTheAskedLacks()
	{
		using C4LSP::Server::ProgramsToConsult;
		using C4LSP::Server::ReachFacts;
		const ReachFacts client{ { "Engine", "Game", "Client" }, { "Engine", "Game", "Client" }, true };
		const ReachFacts tool{ { "Engine", "Tool" }, { "Engine", "Tool" }, false };
		const ReachFacts editor{ { "Engine", "Game", "Editor" }, { "Engine", "Game", "Editor", "Ui" }, false };
		TEST_ASSERT(ProgramsToConsult({ client, tool, editor }, 0) == std::vector<size_t>({ 1, 2 }));
		TEST_ASSERT(ProgramsToConsult({ client, tool }, 1) == std::vector<size_t>({ 0 }));
		// A program that reaches only what the asked one holds adds nothing.
		const ReachFacts within{ { "Engine", "Game" }, { "Engine", "Game" }, true };
		TEST_ASSERT(ProgramsToConsult({ client, within }, 0).empty());
		// A declaration nothing reaches asks no one; asked by none, every reaching project is covered.
		const ReachFacts none{ {}, { "Engine" }, false };
		TEST_ASSERT(ProgramsToConsult({ none, none }, 0).empty());
		TEST_ASSERT(ProgramsToConsult({ client, tool, within }, -1) == std::vector<size_t>({ 0, 1 }));
		TEST_PASSED();
	}
}
