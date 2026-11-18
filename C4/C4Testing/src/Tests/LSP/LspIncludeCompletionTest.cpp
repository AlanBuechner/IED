#include "LspIncludeCompletionTest.h"
#include "LspServerHarness.h"
#include "LspTestSupport.h"
#include "Features/IncludeCompletion.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using C4LSP::Features::IncludeCompletion;
using Json::Value;
using Testing::Lsp::FileUri;
using Testing::Lsp::ServerHarness;
using Testing::Lsp::WriteFile;

namespace Testing
{
	namespace
	{
		const char* kGameMain = "include \"Game/game.ch\"\ninclude \"\"\n\nint32 main()\n{\n\treturn Game::Score();\n}\n";

		struct Workspace
		{
			fs::path root;

			std::string Uri(const std::string& relative) const { return FileUri(root / relative); }

			// The game's program, as IED hands over Nuke's options.
			Value Options() const
			{
				Value options = Value::MakeObject();
				options.SetMember("projects", Value::String(Core::String((root / "build/Game/projects.toml").generic_string())));
				return options;
			}
		};

		// A game on an engine, each with a tests/ folder; beside the headers a source, a text file and a hidden folder.
		Workspace MakeWorkspace(const std::string& name)
		{
			Workspace ws;
			ws.root = fs::temp_directory_path() / name;
			fs::remove_all(ws.root);
			WriteFile(ws.root / "Engine/src/Runtime/module.ch", "namespace Engine\n{\n\tint32 EngineValue();\n}\n");
			WriteFile(ws.root / "Engine/src/Runtime/module.cf", "include \"Runtime/module.ch\"\n\nnamespace Engine\n{\n\tint32 EngineValue() { return 7; }\n}\n");
			WriteFile(ws.root / "Engine/src/Render/pass.ch", "namespace Engine\n{\n\tint32 PassCount();\n}\n");
			WriteFile(ws.root / "Engine/src/tests/engine_tests.cf", "include \"Runtime/module.ch\"\n");
			WriteFile(ws.root / "Game/src/Game/game.ch", "namespace Game\n{\n\tint32 Score();\n}\n");
			WriteFile(ws.root / "Game/src/Game/game.cf", "include \"Game/game.ch\"\n\nnamespace Game\n{\n\tint32 Score() { return 1; }\n}\n");
			WriteFile(ws.root / "Game/src/Game/notes.txt", "not a header\n");
			WriteFile(ws.root / "Game/src/.cache/stale.ch", "\n");
			WriteFile(ws.root / "Game/src/native/native.h", "int native_value(void);\n");
			WriteFile(ws.root / "Game/src/native/native.hpp", "int native_more();\n");
			WriteFile(ws.root / "Game/src/tests/game_tests.cf", "include \"Game/game.ch\"\n");
			WriteFile(ws.root / "Game/src/main.cf", kGameMain);
			WriteFile(ws.root / "build/Game/projects.toml", "root = \"Game\"\n[[project]]\nname = \"Game\"\nsources = [\"" + (ws.root / "Game/src").generic_string() +
				"\"]\ndepends = [\"Engine\"]\n[[project]]\nname = \"Engine\"\nsources = [\"" + (ws.root / "Engine/src").generic_string() + "\"]\n");
			return ws;
		}

		// The completion answer with the caret `columns` past the first `needle` of `text`, asked as typing `trigger` asks
		// (nullptr: asked for, Ctrl+Space or a letter typed).
		Value Complete(ServerHarness& server, const std::string& uri, const std::string& text, const std::string& needle, int64 columns,
			const char* trigger = nullptr)
		{
			Value params = ServerHarness::At(uri, text, needle);
			const Value at = params.Member("position");
			Value moved = Value::MakeObject();
			moved.SetMember("line", Value::Int(at.Member("line").GetInt()));
			moved.SetMember("character", Value::Int(at.Member("character").GetInt() + columns));
			params.SetMember("position", moved);
			Value context = Value::MakeObject();
			context.SetMember("triggerKind", Value::Int(trigger ? 2 : 1));
			if (trigger)
				context.SetMember("triggerCharacter", Value::String(Core::String(trigger)));
			params.SetMember("context", context);
			return server.Request("textDocument/completion", params);
		}

		std::vector<std::string> Labels(const Value& items)
		{
			std::vector<std::string> labels;
			for (uint32 i = 0; items.IsArray() && i < items.Size(); ++i)
				labels.push_back(items.At(i).Member("label").GetString());
			return labels;
		}

		bool Has(const std::vector<std::string>& labels, const std::string& label)
		{
			return std::find(labels.begin(), labels.end(), label) != labels.end();
		}

		Value Item(const Value& items, const std::string& label)
		{
			for (uint32 i = 0; items.IsArray() && i < items.Size(); ++i)
				if (std::string(items.At(i).Member("label").GetString()) == label)
					return items.At(i);
			return Value::Null();
		}

		std::string Detail(const Value& items, const std::string& label) { return Item(items, label).Member("detail").GetString(); }

		// An item's edit as "line:start-end=new text".
		std::string EditOf(const Value& item)
		{
			const Value edit = item.Member("textEdit");
			const Value range = edit.Member("range");
			return std::to_string(range.Member("start").Member("line").GetInt()) + ":" + std::to_string(range.Member("start").Member("character").GetInt()) +
				"-" + std::to_string(range.Member("end").Member("character").GetInt()) + "=" + std::string(edit.Member("newText").GetString());
		}

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return text;
		}
	} // namespace

	void LspIncludeCompletionTest::TestFindsThePathTheCaretIsIn()
	{
		const auto at = [](const std::string& text, size_t line, size_t character) {
			return IncludeCompletion::Find(text, C4LSP::Protocol::Position{ static_cast<uint32>(line), static_cast<uint32>(character) });
		};
		const auto atEnd = [&](const std::string& text) { return at(text, 0, text.size()); };

		const std::string path = "include \"Http/se\"";
		auto site = at(path, 0, path.find("se") + 2);
		TEST_ASSERT(site.has_value());
		TEST_ASSERT_EQ(site->typed, std::string("Http/se"));
		TEST_ASSERT_EQ(site->partStart, static_cast<uint32>(path.find("se")));
		TEST_ASSERT_EQ(site->partEnd, static_cast<uint32>(path.size() - 1)); // up to the closing quote
		TEST_ASSERT(!site->bindings);
		TEST_ASSERT(!site->slashFollows);

		site = at(path, 0, path.find("se") + 1); // `se|e`: the part runs on past the caret
		TEST_ASSERT_EQ(site->typed, std::string("Http/s"));
		TEST_ASSERT_EQ(site->partEnd, static_cast<uint32>(path.size() - 1));

		const std::string inside = "\tinclude \"Http/server.ch\";";
		site = at(inside, 0, inside.find("Ht") + 2); // `Ht|tp/`: a folder of the path
		TEST_ASSERT_EQ(site->typed, std::string("Ht"));
		TEST_ASSERT_EQ(site->partStart, static_cast<uint32>(inside.find("Ht")));
		TEST_ASSERT_EQ(site->partEnd, static_cast<uint32>(inside.find('/')));
		TEST_ASSERT(site->slashFollows);

		site = atEnd("include \"");
		TEST_ASSERT(site.has_value());
		TEST_ASSERT_EQ(site->typed, std::string());

		site = atEnd("extern \"c++\" include \"win");
		TEST_ASSERT(site.has_value());
		TEST_ASSERT(site->bindings);
		TEST_ASSERT_EQ(site->typed, std::string("win"));
		TEST_ASSERT(atEnd("extern \"c\"   include  \"")->bindings);

		const std::string crlf = "int32 a;\r\ninclude \"Ga\r\nint32 b;\r\n";
		site = at(crlf, 1, 11);
		TEST_ASSERT(site.has_value());
		TEST_ASSERT_EQ(site->line, static_cast<uint32>(1));
		TEST_ASSERT_EQ(site->typed, std::string("Ga"));
		TEST_ASSERT_EQ(site->partEnd, static_cast<uint32>(11));

		TEST_ASSERT(!atEnd("include \"a.ch\"").has_value());              // past the closing quote
		TEST_ASSERT(!atEnd("include \"a.ch\" // include \"b").has_value()); // a comment
		TEST_ASSERT(!atEnd("Print(\"include \\\"x").has_value());          // a string that says include
		TEST_ASSERT(!atEnd("xinclude \"a").has_value());
		TEST_ASSERT(!atEnd("char8 c = '\"'; include").has_value());
		TEST_ASSERT(!atEnd("int32 half = 6 /").has_value());
		TEST_ASSERT(!at("include \"a\"", 3, 0).has_value()); // no such line
		TEST_PASSED();
	}

	void LspIncludeCompletionTest::TestOffersWhatAnIncludeReaches()
	{
		const Workspace ws = MakeWorkspace("c4lsp_include_offers");
		{
			ServerHarness server(FileUri(ws.root));
			server.Initialize(ws.Options());
			const std::string main = ws.Uri("Game/src/main.cf");
			server.Open(main, kGameMain);
			const Value items = Complete(server, main, kGameMain, "include \"\"", 9, "\"");
			const std::vector<std::string> labels = Labels(items);
			for (const char* reached : { "Game/", "native/", "Runtime/", "Render/", "bedrock.ch", "Container/" })
				TEST_ASSERT(Has(labels, reached));
			for (const char* outside : { "tests/", "Tests/", ".cache/", "main.cf", "game.ch", "notes.txt" })
				TEST_ASSERT(!Has(labels, outside));

			TEST_ASSERT_EQ(Detail(items, "Game/"), std::string()); // the file's own project
			TEST_ASSERT_EQ(Detail(items, "Runtime/"), std::string("Engine"));
			TEST_ASSERT_EQ(Detail(items, "bedrock.ch"), std::string("Bedrock"));
			TEST_ASSERT_EQ(Item(items, "Game/").Member("kind").GetInt(), int64(19));
			TEST_ASSERT_EQ(Item(items, "bedrock.ch").Member("kind").GetInt(), int64(17));

			// A folder goes in with its '/' and asks for what is in it; a header just goes in.
			TEST_ASSERT_EQ(EditOf(Item(items, "Game/")), std::string("1:9-9=Game/"));
			TEST_ASSERT_EQ(std::string(Item(items, "Game/").Member("command").Member("command").GetString()), std::string("editor.action.triggerSuggest"));
			TEST_ASSERT_EQ(EditOf(Item(items, "bedrock.ch")), std::string("1:9-9=bedrock.ch"));
			TEST_ASSERT(!Item(items, "bedrock.ch").HasMember("command"));

			// In order of name, any case (a folder before the header of its name: `Math/`, `math.ch`), each listed in that order.
			const auto name = [](std::string label) {
				if (!label.empty() && label.back() == '/')
					label.pop_back();
				return Lower(label);
			};
			std::vector<std::string> sorted = labels;
			std::stable_sort(sorted.begin(), sorted.end(), [&](const std::string& a, const std::string& b) { return name(a) < name(b); });
			TEST_ASSERT(sorted == labels);
			const auto math = std::find(labels.begin(), labels.end(), "Math/");
			TEST_ASSERT(math != labels.end() && math + 1 != labels.end() && *(math + 1) == "math.ch");
			for (uint32 i = 1; i < items.Size(); ++i)
				TEST_ASSERT(std::string(items.At(i - 1).Member("sortText").GetString()) < std::string(items.At(i).Member("sortText").GetString()));
		}
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspIncludeCompletionTest::TestCompletesInsideTheFolderTyped()
	{
		const Workspace ws = MakeWorkspace("c4lsp_include_folder");
		{
			ServerHarness server(FileUri(ws.root));
			server.Initialize(ws.Options());
			const std::string main = ws.Uri("Game/src/main.cf");
			const std::string text = "include \"Game/ga\"\ninclude \"Runtime/\"\ninclude \"Container/\"\n\nint32 main()\n{\n\treturn 0;\n}\n";
			server.Open(main, text);

			Value items = Complete(server, main, text, "include \"Game/ga\"", 16); // typing `ga`
			TEST_ASSERT(Labels(items) == std::vector<std::string>({ "game.ch" })); // not its source, not the text file
			TEST_ASSERT_EQ(EditOf(Item(items, "game.ch")), std::string("0:14-16=game.ch"));

			items = Complete(server, main, text, "include \"Runtime/\"", 17, "/");
			TEST_ASSERT(Labels(items) == std::vector<std::string>({ "module.ch" }));
			TEST_ASSERT_EQ(Detail(items, "module.ch"), std::string("Engine"));
			TEST_ASSERT_EQ(EditOf(Item(items, "module.ch")), std::string("1:17-17=module.ch"));

			items = Complete(server, main, text, "include \"Container/\"", 19, "/"); // a folder of the default library
			TEST_ASSERT(Has(Labels(items), "array.ch"));
			TEST_ASSERT_EQ(Detail(items, "array.ch"), std::string("Bedrock"));
			TEST_ASSERT(!Has(Labels(items), "array.cf"));
		}
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspIncludeCompletionTest::TestAFolderInsideAPathAddsNoSlash()
	{
		const Workspace ws = MakeWorkspace("c4lsp_include_inside");
		{
			ServerHarness server(FileUri(ws.root));
			server.Initialize(ws.Options());
			const std::string main = ws.Uri("Game/src/main.cf");
			const std::string text = "include \"Game/game.ch\"\n\nint32 main()\n{\n\treturn 0;\n}\n";
			server.Open(main, text);
			const Value items = Complete(server, main, text, "include", 11); // `Ga|me/game.ch`
			TEST_ASSERT(Labels(items) == std::vector<std::string>({ "Game/" }));
			TEST_ASSERT_EQ(EditOf(Item(items, "Game/")), std::string("0:9-13=Game"));
			TEST_ASSERT(!Item(items, "Game/").HasMember("command"));
		}
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspIncludeCompletionTest::TestAProjectReachesWhatItDependsOn()
	{
		const Workspace ws = MakeWorkspace("c4lsp_include_depends");
		{
			ServerHarness server(FileUri(ws.root));
			server.Initialize(ws.Options());
			const std::string module = ws.Uri("Engine/src/Runtime/module.cf");
			const std::string text = "include \"Runtime/module.ch\"\ninclude \"\"\n\nnamespace Engine\n{\n\tint32 EngineValue() { return 7; }\n}\n";
			server.Open(module, text);
			Value items = Complete(server, module, text, "include \"\"", 9, "\"");
			std::vector<std::string> labels = Labels(items);
			for (const char* reached : { "Runtime/", "Render/", "bedrock.ch" })
				TEST_ASSERT(Has(labels, reached));
			for (const char* outside : { "Game/", "native/", "tests/", "module.cf" })
				TEST_ASSERT(!Has(labels, outside));
			TEST_ASSERT_EQ(Detail(items, "Runtime/"), std::string()); // the engine's own, here

			// The engine's own tests reach its tests/.
			const std::string tests = ws.Uri("Engine/src/tests/engine_tests.cf");
			const std::string testText = "include \"\"\n";
			server.Open(tests, testText);
			labels = Labels(Complete(server, tests, testText, "include", 9));
			TEST_ASSERT(Has(labels, "tests/"));
			TEST_ASSERT(Has(labels, "Runtime/"));
			TEST_ASSERT(!Has(labels, "Game/"));
		}
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspIncludeCompletionTest::TestBindingsOfferCHeaders()
	{
		const Workspace ws = MakeWorkspace("c4lsp_include_bindings");
		{
			ServerHarness server(FileUri(ws.root));
			server.Initialize(ws.Options());
			const std::string main = ws.Uri("Game/src/main.cf");
			const std::string text = "extern \"c\" include \"native/\"\nextern \"c++\" include \"\"\n\nint32 main()\n{\n\treturn 0;\n}\n";
			server.Open(main, text);
			Value items = Complete(server, main, text, "native/", 7, "/");
			TEST_ASSERT(Labels(items) == std::vector<std::string>({ "native.h", "native.hpp" }));

			const std::vector<std::string> labels = Labels(Complete(server, main, text, "include \"\"", 9, "\""));
			TEST_ASSERT(Has(labels, "native/"));
			TEST_ASSERT(Has(labels, "Game/"));
			TEST_ASSERT(!Has(labels, "bedrock.ch"));
			TEST_ASSERT(!Has(labels, "Container/"));
		}
		fs::remove_all(ws.root);
		TEST_PASSED();
	}

	void LspIncludeCompletionTest::TestQuotesAndSlashesElsewhereAnswerNothing()
	{
		const Workspace ws = MakeWorkspace("c4lsp_include_elsewhere");
		{
			ServerHarness server(FileUri(ws.root));
			server.Initialize(ws.Options());
			const std::string main = ws.Uri("Game/src/main.cf");
			const std::string text = "include \"Game/game.ch\"\n\nint32 main()\n{\n\tint32 half = 6 / 2; // halves\n\treturn half + Count(\"abc\");\n}\n";
			server.Open(main, text);
			TEST_ASSERT(Complete(server, main, text, "6 /", 3, "/").IsNull());     // a division
			TEST_ASSERT(Complete(server, main, text, "// halves", 2, "/").IsNull()); // a comment
			TEST_ASSERT(Complete(server, main, text, "Count(\"", 7, "\"").IsNull()); // a string
			TEST_ASSERT(Has(Labels(Complete(server, main, text, "half +", 2)), "half")); // names, as before
		}
		fs::remove_all(ws.root);
		TEST_PASSED();
	}
} // namespace Testing
