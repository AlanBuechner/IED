#include "LspAnalysesTest.h"
#include "LspServerHarness.h"
#include "LspTestSupport.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

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
		// An engine module, a client program on it and a tool program on it.
		const char* kEngineHeader = "namespace Engine\n{\n\tint32 EngineValue();\n}\n";
		const char* kEngineSource = "include \"Runtime/module.ch\"\n\nnamespace Engine\n{\n\tint32 EngineValue() { return 7; }\n}\n";
		const char* kClientMain = "include \"Runtime/module.ch\"\n\nint32 main()\n{\n\treturn Engine::EngineValue();\n}\n";
		const char* kToolMain = "include \"Runtime/module.ch\"\n\nnamespace Tool\n{\n\tclass Module\n\t{\n\t\tpublic int32 Run() { return Engine::EngineValue() + 1; }\n\t}\n}\n\n"
			"int32 main()\n{\n\tTool::Module module;\n\treturn module.Run();\n}\n";

		// Longer than an answer from an analysis already made takes, shorter than a request's wait for one (10 s).
		constexpr auto kAtOnce = std::chrono::seconds(3);
		// Longer than the worker's look at the disk (every 2 s) and the analyses it starts take.
		constexpr auto kNoticed = std::chrono::seconds(30);

		struct Workspace
		{
			fs::path root;
			std::string engineHeader, engineSource, clientMain, toolMain; // URIs

			Value WithPrograms() const
			{
				Value programs = Value::MakeObject();
				for (const char* name : { "Client", "Tool" })
				{
					Value options = Value::MakeObject();
					options.SetMember("projects", Value::String(Core::String((root / "build" / name / "projects.toml").generic_string())));
					programs.SetMember(name, options);
				}
				Value options = Value::MakeObject();
				options.SetMember("programs", programs);
				return options;
			}
		};

		// Removes a scratch workspace when it goes.
		struct Removed
		{
			fs::path root;
			~Removed()
			{
				std::error_code ignored;
				fs::remove_all(root, ignored);
			}
		};

		Workspace MakeWorkspace(const std::string& name)
		{
			Workspace ws;
			ws.root = fs::temp_directory_path() / name;
			fs::remove_all(ws.root);
			WriteFile(ws.root / "Engine/src/Runtime/module.ch", kEngineHeader);
			WriteFile(ws.root / "Engine/src/Runtime/module.cf", kEngineSource);
			WriteFile(ws.root / "Client/src/main.cf", kClientMain);
			WriteFile(ws.root / "Tool/src/main.cf", kToolMain);
			const std::string engine = "[[project]]\nname = \"Engine\"\nsources = [\"" + (ws.root / "Engine/src").generic_string() + "\"]\n";
			for (const char* program : { "Client", "Tool" })
				WriteFile(ws.root / "build" / program / "projects.toml", std::string("root = \"") + program + "\"\n[[project]]\nname = \"" + program + "\"\nsources = [\"" +
					(ws.root / program / "src").generic_string() + "\"]\ndepends = [\"Engine\"]\n" + engine);
			ws.engineHeader = FileUri(ws.root / "Engine/src/Runtime/module.ch");
			ws.engineSource = FileUri(ws.root / "Engine/src/Runtime/module.cf");
			ws.clientMain = FileUri(ws.root / "Client/src/main.cf");
			ws.toolMain = FileUri(ws.root / "Tool/src/main.cf");
			return ws;
		}

		Value Document(const std::string& uri)
		{
			Value document = Value::MakeObject();
			document.SetMember("uri", Value::String(Core::String(uri)));
			Value params = Value::MakeObject();
			params.SetMember("textDocument", document);
			return params;
		}

		// Whether `holds` came true within `most`, looked at every tenth of a second.
		bool Within(std::chrono::seconds most, const std::function<bool()>& holds)
		{
			const auto until = std::chrono::steady_clock::now() + most;
			while (!holds())
			{
				if (std::chrono::steady_clock::now() > until) return false;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			return true;
		}

		// How long `request` takes.
		std::chrono::steady_clock::duration Timed(const std::function<void()>& request)
		{
			const auto start = std::chrono::steady_clock::now();
			request();
			return std::chrono::steady_clock::now() - start;
		}

		// The names of a documentSymbol answer's top-level symbols, one per line.
		std::string NamesOf(const Value& symbols)
		{
			std::string names;
			for (uint32 i = 0; i < symbols.Size(); ++i) names += std::string(symbols.At(i).Member("name").GetString()) + "\n";
			return names;
		}

		Value Position(uint32 line, uint32 character)
		{
			Value at = Value::MakeObject();
			at.SetMember("line", Value::Int(line));
			at.SetMember("character", Value::Int(character));
			return at;
		}

		// A didChange content change putting `text` over [`from`, `to`) of `line`.
		Value Splice(uint32 line, uint32 from, uint32 to, const std::string& text)
		{
			Value range = Value::MakeObject();
			range.SetMember("start", Position(line, from));
			range.SetMember("end", Position(line, to));
			Value change = Value::MakeObject();
			change.SetMember("range", range);
			change.SetMember("text", Value::String(Core::String(text)));
			return change;
		}
	}

	void LspAnalysesTest::TestOpeningOrClosingAnUnchangedFileAnalyzesNothing()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_unchanged");
		const Removed removed{ ws.root }; // after the server is gone: its worker may be reading the folder
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.clientMain, kClientMain);
		const size_t first = server.Server().AnalysesMade();
		TEST_ASSERT(first >= 1);
		server.Open(ws.engineSource, kEngineSource); // its file's text: the client's analysis compiled it already
		TEST_ASSERT(Contains(NamesOf(server.Request("textDocument/documentSymbol", Document(ws.engineSource))), "Engine"));
		server.Close(ws.engineSource);
		TEST_ASSERT_EQ(server.Server().AnalysesMade(), first);
		server.Open(ws.engineSource, std::string(kEngineSource) + "\nint32 Extra() { return 1; }\n"); // other text than its file
		TEST_ASSERT_EQ(server.Server().AnalysesMade(), first + 1);
		TEST_PASSED();
	}

	void LspAnalysesTest::TestClosingAnEditedDocumentAnalyzesItsFileAgain()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_edited");
		const Removed removed{ ws.root }; // after the server is gone: its worker may be reading the folder
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.clientMain, kClientMain);
		server.Open(ws.engineHeader, "namespace Engine\n{\n\tint32 Renamed();\n}\n"); // unsaved: the client's call names nothing
		TEST_ASSERT(Contains(server.MessagesFor(ws.clientMain), "EngineValue"));
		const size_t edited = server.Server().AnalysesMade();
		server.Close(ws.engineHeader); // its file was never changed: the program reads it again
		TEST_ASSERT_EQ(server.Server().AnalysesMade(), edited + 1);
		TEST_ASSERT_EQ(server.MessagesFor(ws.clientMain), std::string());
		TEST_PASSED();
	}

	void LspAnalysesTest::TestARequestWaitsForItsOwnProgramAlone()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_own_program");
		const Removed removed{ ws.root }; // after the server is gone: its worker may be reading the folder
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.clientMain, kClientMain);
		server.Open(ws.toolMain, kToolMain);
		server.Server().HoldAnalysis(true); // as on a program too large to analyze between two requests
		server.Change(ws.clientMain, std::string(kClientMain) + "\n// edited\n");
		Value hover;
		const auto took = Timed([&] { hover = server.Request("textDocument/hover", ServerHarness::At(ws.toolMain, kToolMain, "Run()")); });
		server.Server().HoldAnalysis(false);
		TEST_ASSERT(took < kAtOnce);
		TEST_ASSERT(hover.IsObject());
		TEST_PASSED();
	}

	void LspAnalysesTest::TestAnOutlineWaitsForItsOwnDocumentAlone()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_own_document");
		const Removed removed{ ws.root }; // after the server is gone: its worker may be reading the folder
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.engineHeader, kEngineHeader);
		server.Open(ws.engineSource, kEngineSource);
		server.Server().HoldAnalysis(true);
		server.Change(ws.engineHeader, std::string(kEngineHeader) + "\n// edited\n"); // the source's program, pending
		Value outline;
		const auto took = Timed([&] { outline = server.Request("textDocument/documentSymbol", Document(ws.engineSource)); });
		server.Server().HoldAnalysis(false);
		TEST_ASSERT(took < kAtOnce);
		TEST_ASSERT(Contains(NamesOf(outline), "Engine"));
		TEST_PASSED();
	}

	void LspAnalysesTest::TestAFileChangedOnDiskIsAnalyzedAgain()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_disk");
		const Removed removed{ ws.root }; // after the server is gone: its worker may be reading the folder
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.clientMain, kClientMain);
		TEST_ASSERT_EQ(server.MessagesFor(ws.clientMain), std::string());
		const fs::path header = ws.root / "Engine/src/Runtime/module.ch";
		WriteFile(header, "namespace Engine\n{\n\tint32 Renamed();\n}\n"); // git, a build or another tool: no editor in between
		TEST_ASSERT(Within(kNoticed, [&] { return Contains(server.MessagesFor(ws.clientMain), "EngineValue"); }));
		WriteFile(header, kEngineHeader);
		TEST_ASSERT(Within(kNoticed, [&] { return server.MessagesFor(ws.clientMain).empty(); }));
		TEST_PASSED();
	}

	void LspAnalysesTest::TestABrokenFileKeepsTheBodyErrorsOfTheOthers()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_kept_bodies");
		const Removed removed{ ws.root };
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		const std::string client = "include \"Runtime/module.ch\"\n\nint32 main()\n{\n\tint64 wrong = \"text\";\n\treturn Engine::EngineValue();\n}\n";
		server.Open(ws.clientMain, client);
		server.Open(ws.engineSource, kEngineSource);
		TEST_ASSERT(Contains(server.MessagesFor(ws.clientMain), "cannot initialize"));
		server.Change(ws.engineSource, "include \"Runtime/module.ch\"\n\nnamespace Engine\n{\n\tint32 EngineValue() { return 7 }\n}\n"); // mid-edit
		TEST_ASSERT(!server.MessagesFor(ws.engineSource).empty());
		TEST_ASSERT(Contains(server.MessagesFor(ws.clientMain), "cannot initialize")); // not fixed by the other file's typo
		server.Change(ws.clientMain, client + "\n"); // its own text changed: what its bodies say is not known until they lower
		TEST_ASSERT_EQ(server.MessagesFor(ws.clientMain), std::string());
		server.Change(ws.engineSource, kEngineSource);
		TEST_ASSERT(Contains(server.MessagesFor(ws.clientMain), "cannot initialize"));
		TEST_PASSED();
	}

	void LspAnalysesTest::TestASearchAnswersFromTheBackgroundIndex()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_index");
		const Removed removed{ ws.root }; // after the server is gone: its worker may be reading the folder
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.clientMain, kClientMain); // the tool's program is analyzed for nothing the editor has open
		TEST_ASSERT(Within(kNoticed, [&] { return server.Server().IndexedProgramCount() >= 2; }));
		TEST_ASSERT_EQ(server.Server().AnalyzedProgramCount(), (size_t)1); // the index's analysis was let go at once
		server.Server().HoldAnalysis(true); // a search that needed an analysis would wait for it
		Value query = Value::MakeObject();
		query.SetMember("query", Value::String(Core::String("Module")));
		Value found;
		const auto took = Timed([&] { found = server.Request("workspace/symbol", query); });
		server.Server().HoldAnalysis(false);
		TEST_ASSERT(took < kAtOnce);
		bool toolModule = false;
		for (uint32 i = 0; i < found.Size(); ++i)
			toolModule = toolModule || (found.At(i).Member("name").GetString() == "Module" && found.At(i).Member("containerName").GetString() == "Tool");
		TEST_ASSERT(toolModule);
		TEST_PASSED();
	}

	void LspAnalysesTest::TestASearchTakesAnOpenDocumentsSymbolsFromItsAnalysis()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_index_open");
		const Removed removed{ ws.root }; // after the server is gone: its worker may be reading the folder
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms());
		server.Open(ws.clientMain, kClientMain);
		TEST_ASSERT(Within(kNoticed, [&] { return server.Server().IndexedProgramCount() >= 2; }));
		server.Open(ws.engineHeader, "namespace Engine\n{\n\tint32 EngineValueRenamed();\n}\n"); // the tool's index has the file's
		Value query = Value::MakeObject();
		query.SetMember("query", Value::String(Core::String("EngineValue")));
		const Value found = server.Request("workspace/symbol", query);
		bool renamed = false, old = false;
		for (uint32 i = 0; i < found.Size(); ++i)
		{
			const Value symbol = found.At(i);
			if (std::string(symbol.Member("location").Member("uri").GetString()) != ws.engineHeader) continue;
			renamed = renamed || symbol.Member("name").GetString() == "EngineValueRenamed";
			old = old || symbol.Member("name").GetString() == "EngineValue";
		}
		TEST_ASSERT(renamed);
		TEST_ASSERT(!old);
		TEST_PASSED();
	}

	void LspAnalysesTest::TestARangedChangeEditsWhereItSays()
	{
		const Workspace ws = MakeWorkspace("c4lsp_analyses_ranged");
		const Removed removed{ ws.root }; // after the server is gone: its worker may be reading the folder
		// An accent before the name: its UTF-16 column (14) is not its byte (15).
		const std::string text = "// extra\n/* \xC3\xA9 */ int32 A() { return 1; }\n";
		WriteFile(ws.root / "Client/src/extra.cf", text);
		const std::string uri = FileUri(ws.root / "Client/src/extra.cf");
		ServerHarness server(FileUri(ws.root));
		server.Initialize(ws.WithPrograms()); // the client offers no encoding: UTF-16 columns
		server.Open(uri, text);
		Value changes = Value::MakeArray();
		changes.Append(Splice(1, 14, 15, "C")); // A renamed C
		changes.Append(Splice(1, 0, 0, "int32 B() { return 2; }\n")); // then a line above it, where the first change left it
		Value document = Value::MakeObject();
		document.SetMember("uri", Value::String(Core::String(uri)));
		document.SetMember("version", Value::Int(2));
		Value params = Value::MakeObject();
		params.SetMember("textDocument", document);
		params.SetMember("contentChanges", changes);
		server.Notify("textDocument/didChange", params);
		const Value outline = server.Request("textDocument/documentSymbol", Document(uri));
		TEST_ASSERT_EQ(NamesOf(outline), std::string("B\nC\n"));
		TEST_ASSERT_EQ(outline.At(0).Member("selectionRange").Member("start").Member("line").GetInt(), (int64)1);
		TEST_ASSERT_EQ(outline.At(1).Member("selectionRange").Member("start").Member("line").GetInt(), (int64)2);
		TEST_ASSERT_EQ(outline.At(1).Member("selectionRange").Member("start").Member("character").GetInt(), (int64)14);
		TEST_ASSERT_EQ(server.MessagesFor(uri), std::string());
		TEST_PASSED();
	}
} // namespace Testing
