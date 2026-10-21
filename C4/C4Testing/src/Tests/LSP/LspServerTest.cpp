#include "LspServerTest.h"
#include "LspTestSupport.h"
#include "Features/Tidy.h" // server-side tidy on/off flag (driven by initializationOptions / didChangeConfiguration)
#include "Features/CodeActions.h" // server-side code-action flags (same mechanism)
#include "Compiler/Toolchain.h"
#include "Tests/Script/TestHosts.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>

namespace fs = std::filesystem;

using namespace Json;
using Testing::Lsp::Contains;

namespace Testing
{
	void LspServerTest::Reset(const std::string& rootUri)
	{
		// Destroy the previous server FIRST (its destructor joins the recompile worker), then clear.
		// Clearing before the old worker is gone let a stale notification from the prior test leak
		// into this test's captured output. m_Framed must be cleared too (Reset does): Refresh() replays every
		// frame in it, so a prior phase's diagnostics would otherwise reappear after the clear.
		m_Server.reset();
		m_Framed.clear();
		m_Messages.clear();
		m_Server = std::make_unique<C4LSP::Server::LspServer>([this](const std::string& framed) {
			m_Framed.push_back(framed);
		});
	}

	void LspServerTest::FeedRequest(const std::string& method, int64 id, const Value& params)
	{
		Value msg = Value::MakeObject();
		msg.SetMember("jsonrpc", Value::String(Core::String("2.0")));
		msg.SetMember("id", Value::Int(id));
		msg.SetMember("method", Value::String(Core::String(method)));
		msg.SetMember("params", params);
		m_Server->HandleMessage(msg);
	}

	void LspServerTest::FeedNotification(const std::string& method, const Value& params)
	{
		Value msg = Value::MakeObject();
		msg.SetMember("jsonrpc", Value::String(Core::String("2.0")));
		msg.SetMember("method", Value::String(Core::String(method)));
		msg.SetMember("params", params);
		m_Server->HandleMessage(msg);
	}

	Value LspServerTest::MakeTextDocParams(const std::string& uri, const std::string& text)
	{
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(uri)));
		doc.SetMember("languageId", Value::String(Core::String("c4")));
		doc.SetMember("version", Value::Int(1));
		doc.SetMember("text", Value::String(Core::String(text)));

		Value params = Value::MakeObject();
		params.SetMember("textDocument", doc);
		return params;
	}

	void LspServerTest::Refresh()
	{
		m_Messages.clear();
		for (const auto& framed : m_Framed)
		{
			size_t sep = framed.find("\r\n\r\n");
			if (sep == std::string::npos)
				continue;
			std::string body = framed.substr(sep + 4);
			m_Messages.push_back(Value(Core::LoadObjectDescriptionFromJsonString(body)));
		}
	}

	const Value* LspServerTest::ResponseForId(int64 id)
	{
		Refresh();
		for (const auto& msg : m_Messages)
		{
			if (!msg.HasMember("id"))
				continue;
			if (msg.Member("id").GetInt() == id && (msg.HasMember("result") || msg.HasMember("error")))
				return &msg;
		}
		return nullptr;
	}

	const Value* LspServerTest::NotificationByMethod(const std::string& method)
	{
		Refresh();
		for (const auto& msg : m_Messages)
		{
			if (msg.HasMember("id"))
				continue; // skip responses
			if (msg.Member("method").GetString() == method)
				return &msg;
		}
		return nullptr;
	}

	const Value* LspServerTest::NotificationByMethod(const std::string& method, size_t* cursor)
	{
		Refresh();
		for (size_t i = *cursor; i < m_Messages.size(); ++i)
		{
			const auto& msg = m_Messages[i];
			if (msg.HasMember("id"))
				continue; // skip responses
			if (msg.Member("method").GetString() == method)
			{
				*cursor = i + 1;
				return &msg;
			}
		}
		return nullptr;
	}

	void LspServerTest::TestInitializeAdvertisesCapabilities()
	{
		Reset("file:///root");

		Value params = Value::MakeObject();
		params.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, params);

		const Value* resp = ResponseForId(1);
		TEST_ASSERT_NOT_NULL(resp);
		const Value& caps = resp->Member("result").Member("capabilities");
		TEST_ASSERT_EQ(caps.Member("hoverProvider").GetBool(), true);
		TEST_ASSERT_EQ(caps.Member("definitionProvider").GetBool(), true);
		TEST_ASSERT_EQ(caps.Member("documentSymbolProvider").GetBool(), true);
		TEST_ASSERT(caps.HasMember("completionProvider"));
		TEST_ASSERT(caps.HasMember("textDocumentSync"));
		// BUG-295: completion triggers on the character that ends an accessor - `>` of `->`, never `-` (subtraction).
		const Value& triggers = caps.Member("completionProvider").Member("triggerCharacters");
		std::vector<std::string> trigger;
		for (uint32 i = 0; i < triggers.Size(); ++i)
			trigger.push_back(triggers.At(i).GetString());
		TEST_ASSERT(trigger == std::vector<std::string>({ ".", ">" }));
		TEST_PASSED();
	}

	void LspServerTest::TestHoverEndToEndReturnsType()
	{
		const std::string program = "int32 g_value = 10;\nvoid Run() { int32 v = g_value; }\n";
		std::string uri = Testing::Lsp::UriFor("HoverDoc.cf");

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork(); // let the async recompile swap in a built snapshot

		auto pos = Testing::Lsp::PositionOf(program, "g_value", 2); // the use
		Value hoverParams = Value::MakeObject();
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(uri)));
		hoverParams.SetMember("textDocument", doc);
		Value position = Value::MakeObject();
		position.SetMember("line", Value::Int(pos.line));
		position.SetMember("character", Value::Int(pos.character));
		hoverParams.SetMember("position", position);

		FeedRequest("textDocument/hover", 2, hoverParams);

		const Value* resp = ResponseForId(2);
		TEST_ASSERT_NOT_NULL(resp);
		Value result = resp->Member("result");
		TEST_ASSERT(result.IsObject()); // a found hover is an object, not null
		TEST_ASSERT(Contains(result.Member("contents").GetString(), "int32"));
		TEST_PASSED();
	}

	void LspServerTest::TestSemanticTokensCapabilityAndFullRequest()
	{
		const std::string program = "int32 g_value = 10;\nvoid Run() { int32 v = g_value; }\n";
		std::string uri = Testing::Lsp::UriFor("SemDoc.cf");

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);

		// The capability must advertise a semanticTokensProvider with the standard C++ legend.
		const Value* initResp = ResponseForId(1);
		TEST_ASSERT_NOT_NULL(initResp);
		const Value& caps = initResp->Member("result").Member("capabilities");
		const Value& stp = caps.Member("semanticTokensProvider");
		TEST_ASSERT(stp.IsObject());
		TEST_ASSERT_EQ(stp.Member("full").GetBool(), true);
		const Value& legend = stp.Member("legend");
		Value types = legend.Member("tokenTypes");
		TEST_ASSERT(types.IsArray());
		TEST_ASSERT_EQ((int64)types.Size(), (int64)21); // standard C++-like legend + string, plus the controlFlow and preprocessor extension kinds
		TEST_ASSERT(legend.Member("tokenModifiers").IsArray());

		// The C4 extension kinds come last, so existing indices stay stable.
		TEST_ASSERT(types.At(19).GetString() == "controlFlow");
		TEST_ASSERT(types.At(20).GetString() == "preprocessor");

		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork(); // let the async recompile swap in a built snapshot

		// Request full-document semantic tokens.
		Value params = Value::MakeObject();
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(uri)));
		params.SetMember("textDocument", doc);
		FeedRequest("textDocument/semanticTokens/full", 42, params);

		const Value* resp = ResponseForId(42);
		TEST_ASSERT_NOT_NULL(resp);
		Value result = resp->Member("result");
		TEST_ASSERT(result.IsObject()); // non-empty document -> object with `data`
		Value data = result.Member("data");
		TEST_ASSERT(data.IsArray());
		TEST_ASSERT_EQ((int64)(data.Size() % 5), (int64)0); // tokens are 5-field deltas
		TEST_ASSERT(data.Size() >= (uint32)15);             // at least 3 tokens

		// The very first token must be the `int32` type keyword: line 0, char 0, length 5, kind 1 ("type").
		TEST_ASSERT_EQ((int64)data.At(0).GetInt(0), (int64)0); // line delta 0
		TEST_ASSERT_EQ((int64)data.At(1).GetInt(0), (int64)0); // char delta 0
		TEST_ASSERT_EQ((int64)data.At(2).GetInt(0), (int64)5); // "int32" length
		TEST_ASSERT_EQ((int64)data.At(3).GetInt(0), (int64)1); // kind id 1 == "type"
		TEST_PASSED();
	}

	void LspServerTest::TestSemanticTokensWaitForPendingWorkWithoutExplicitSync()
	{
		const std::string program = "int32 g_value = 10;\nvoid Run() { int32 v = g_value; }\n";
		std::string uri = Testing::Lsp::UriFor("SemNoSync.cf");

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));

		// No WaitForCurrentWork() here on purpose: this is exactly what a client like Monaco does. It
		// fetches semanticTokens/full once when its provider activates and (in the editor) refetches every time
		// the server publishes diagnostics - so the handler must NOT block for pending work; it answers
		// immediately from whatever snapshot exists, which before the first recompile is empty.
		Value params = Value::MakeObject();
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(uri)));
		params.SetMember("textDocument", doc);

		auto start = std::chrono::steady_clock::now();
		FeedRequest("textDocument/semanticTokens/full", 43, params);
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

		// The handler must not wait for the pending recompile: it answers from the pre-compile snapshot in
		// microseconds. (Every other read-only feature blocks up to 10s here via SnapshotForRequest; this
		// test would take >10s if semantic tokens regressed to that behavior.)
		TEST_ASSERT(elapsedMs < 100);

		const Value* resp = ResponseForId(43);
		TEST_ASSERT_NOT_NULL(resp);
		Value result = resp->Member("result");
		TEST_ASSERT(result.IsNull()); // pre-compile snapshot has no AST for this document -> null (no tokens)

		// Recovery path, exactly what the editor's provider does: the publishDiagnostics notification that the
		// recompile worker emits is its "onDidChange" signal. By then the built snapshot exists and a
		// refetch returns real tokens - no waiting inside the handler needed.
		m_Server->WaitForCurrentWork(); // let the worker finish (a live client's fetch would race it)
		const Value* publish = NotificationByMethod("textDocument/publishDiagnostics");
		TEST_ASSERT_NOT_NULL(publish);
		TEST_ASSERT(Contains(publish->Member("params").Member("uri").GetString(), uri)); // this document's publish triggered it

		FeedRequest("textDocument/semanticTokens/full", 44, params); // the publish-triggered refetch
		const Value* resp2 = ResponseForId(44);
		TEST_ASSERT_NOT_NULL(resp2);
		Value result2 = resp2->Member("result");
		TEST_ASSERT(result2.IsObject()); // non-null now: the built snapshot has this document's AST
		Value data = result2.Member("data");
		TEST_ASSERT(data.IsArray());
		TEST_ASSERT_EQ((int64)(data.Size() % 5), (int64)0);
		TEST_ASSERT(data.Size() >= (uint32)15); // non-empty: at least 3 tokens

		// The very first token must be the `int32` type keyword: line 0, char 0, length 5, kind 1 ("type").
		TEST_ASSERT_EQ((int64)data.At(0).GetInt(0), (int64)0); // line delta 0
		TEST_ASSERT_EQ((int64)data.At(1).GetInt(0), (int64)0); // char delta 0
		TEST_ASSERT_EQ((int64)data.At(2).GetInt(0), (int64)5); // "int32" length
		TEST_ASSERT_EQ((int64)data.At(3).GetInt(0), (int64)1); // kind id 1 == "type"
		TEST_PASSED();
	}

	void LspServerTest::TestCompletionAfterDidChangeWaitsForRecompileWithoutExplicitSync()
	{
		const std::string openProgram = "struct Widget {\n\tvoid Init();\n};\n\nvoid Run() {\n\tWidget widget;\n}\n";
		std::string uri = Testing::Lsp::UriFor("CompNoSync.cf");

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, openProgram));

		// Type a member access on the next line (full-text didChange, like the editor/Monaco) and request completion
		// at "widget.|" immediately - no WaitForCurrentWork() anywhere: exactly what an editor does when '.' is
		// typed. If the handler answered from the pre-recompile snapshot it would see a document that has no
		// `widget.Init` line at all (the offset lands past its end), so member resolution fails and only global
		// symbols are offered - which is precisely what this test catches.
		const std::string finalText = "struct Widget {\n\tvoid Init();\n};\n\nvoid Run() {\n\tWidget widget;\n\twidget.Init();\n}\n";

		Value docChange = Value::MakeObject();
		docChange.SetMember("uri", Value::String(Core::String(uri)));
		docChange.SetMember("version", Value::Int(2));
		Value changeArray = Value::MakeArray();
		Value fullText = Value::MakeObject(); // no "range" member -> a full-document replacement (what C4LSP's fullSync path expects)
		fullText.SetMember("text", Value::String(Core::String(finalText)));
		changeArray.Append(fullText);
		// LSP spec shape (and exactly what the editor's LSP bridge frames): contentChanges is a SIBLING of textDocument under params.
		Value changeParams = Value::MakeObject();
		changeParams.SetMember("textDocument", docChange);
		changeParams.SetMember("contentChanges", changeArray);
		FeedNotification("textDocument/didChange", changeParams);

		auto pos = Testing::Lsp::PositionOf(finalText, "widget.Init"); // start of "widget" (line 5)
		pos.character += 11; // caret right after "Init": w i d g e t . I n i t -> member prefix is "Init"
		Value params = Value::MakeObject();
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(uri)));
		params.SetMember("textDocument", doc);
		Value position = Value::MakeObject();
		position.SetMember("line", Value::Int(pos.line));
		position.SetMember("character", Value::Int(pos.character));
		params.SetMember("position", position);
		FeedRequest("textDocument/completion", 44, params);

		const Value* resp = ResponseForId(44);
		TEST_ASSERT_NOT_NULL(resp);
		Value result = resp->Member("result");
		bool hasInit = false;
		if (result.IsArray())
			for (uint32 i = 0; i < result.Size(); ++i)
				hasInit |= result.At(i).Member("label").GetString() == "Init"; // the Widget member via pointer-agnostic unwrap
		TEST_ASSERT(hasInit);

		m_Server->WaitForCurrentWork(); // let the worker finish before Reset destroys it
		TEST_PASSED();
	}

	void LspServerTest::TestDidOpenPublishesEmptyDiagnosticsForValidProgram()
	{
		const std::string program = "int32 g_value = 10;\nint32 g_sink;\nvoid Run() { int32 v = g_value; g_sink = v; }\n";
		std::string uri = Testing::Lsp::UriFor("ValidDoc.cf");

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork(); // recompile is async; wait so the assertion below is deterministic

		const Value* pub = NotificationByMethod("textDocument/publishDiagnostics");
		TEST_ASSERT_NOT_NULL(pub);
		TEST_ASSERT_EQ(pub->Member("params").Member("uri").GetString(), uri);
		Value diags = pub->Member("params").Member("diagnostics");
		TEST_ASSERT(diags.IsArray());
		TEST_ASSERT_EQ(diags.Size(), (uint32)0); // no diagnostics for a valid program
		TEST_PASSED();
	}

	void LspServerTest::TestDidOpenPublishesErrorsForInvalidProgram()
	{
		const std::string program = "void Run() {\n\tint32 v = missing_var;\n}\n";
		std::string uri = Testing::Lsp::UriFor("BadDoc.cf");

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork(); // recompile is async; wait so the assertion below is deterministic

		const Value* pub = NotificationByMethod("textDocument/publishDiagnostics");
		TEST_ASSERT_NOT_NULL(pub);
		Value diags = pub->Member("params").Member("diagnostics");
		TEST_ASSERT(diags.IsArray());
		TEST_ASSERT_GE(diags.Size(), (uint32)1); // at least one diagnostic for the undeclared identifier

		// The diagnostic should reference the offending identifier.
		bool mentionsMissingVar = false;
		for (uint32 i = 0; i < diags.Size(); ++i)
			if (Contains(diags.At(i).Member("message").GetString(), "missing_var"))
				mentionsMissingVar = true;
		TEST_ASSERT(mentionsMissingVar);
		TEST_PASSED();
	}

	void LspServerTest::TestSemanticDiagnosticsCarryNonZeroWidthRanges()
	{
		const std::string program = "struct S {\n\tpublic float a;\n}\nvoid Run(S v) {\n\tv.a = 1.0f;\n\tNotARealSymbol(v);\n}\n";
		std::string uri = Testing::Lsp::UriFor("SpanDoc.cf");

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork();

		const Value* pub = NotificationByMethod("textDocument/publishDiagnostics");
		TEST_ASSERT_NOT_NULL(pub);
		Value diags = pub->Member("params").Member("diagnostics");
		TEST_ASSERT(diags.IsArray());

		// The undefined-identifier diagnostic must not be a zero-width point: Monaco renders such
		// markers invisibly (no squiggle), which would hide the error from the editor.
		bool foundUndefined = false;
		for (uint32 i = 0; i < diags.Size(); ++i)
		{
			if (!Contains(diags.At(i).Member("message").GetString(), "NotARealSymbol"))
				continue;
			foundUndefined = true;
			const Value& range = diags.At(i).Member("range");
			uint32 startCol = (uint32)range.Member("start").Member("character").GetInt();
			uint32 endCol = (uint32)range.Member("end").Member("character").GetInt();
			TEST_ASSERT_EQ((uint32)range.Member("start").Member("line").GetInt(), (uint32)5); // 0-based line of the call
			// The squiggle must cover exactly the identifier `NotARealSymbol` (columns 1..15 exclusive).
			TEST_ASSERT_EQ(startCol, (uint32)1);
			TEST_ASSERT_EQ(endCol, (uint32)15);
		}
		TEST_ASSERT(foundUndefined);
		TEST_PASSED();
	}

	void LspServerTest::TestMethodArgTypeErrorTargetsOffendingArgument()
	{
		const std::string program = "struct S {\n\tpublic void Scale(float x) { }\n}\nvoid Run(S v) {\n\tv.Scale(\"oops\");\n}\n";
		std::string uri = Testing::Lsp::UriFor("ArgSpanDoc.cf");

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork();

		const Value* pub = NotificationByMethod("textDocument/publishDiagnostics");
		TEST_ASSERT_NOT_NULL(pub);
		Value diags = pub->Member("params").Member("diagnostics");
		TEST_ASSERT(diags.IsArray());

		// An argument-type error must point at the offending ARGUMENT expression, not at the call's
		// start (the receiver). A point range under `v` renders as an invisible squiggle in Monaco;
		// spanning `"oops"` is what Visual Assist-style editors expect.
		uint32 matching = 0;
		for (uint32 i = 0; i < diags.Size(); ++i)
		{
			if (!Contains(diags.At(i).Member("message").GetString(), "argument 1 of Scale has type 'char8*' but parameter 'x' expects 'float'"))
				continue;
			++matching;
			const Value& range = diags.At(i).Member("range");
			uint32 startLine = (uint32)range.Member("start").Member("line").GetInt();
			uint32 endLine = (uint32)range.Member("end").Member("line").GetInt();
			uint32 startCol = (uint32)range.Member("start").Member("character").GetInt();
			uint32 endCol = (uint32)range.Member("end").Member("character").GetInt();
			TEST_ASSERT_EQ(startLine, (uint32)4); // 0-based line of the call statement
			TEST_ASSERT_EQ(endLine, startLine);   // single-line span
			// `v.Scale(` is 9 chars: the string literal starts at column 9 and ends after its closing quote.
			TEST_ASSERT_EQ(startCol, (uint32)9);
			TEST_ASSERT_EQ(endCol, (uint32)15);
		}
		TEST_ASSERT_EQ(matching, (uint32)1); // exactly one such diagnostic: no duplicates from the include-merge
		TEST_PASSED();
	}

	void LspServerTest::TestUnknownMethodReturnsMethodNotFound()
	{
		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());

		FeedRequest("textDocument/bogusMethod", 9, Value::Null());

		const Value* resp = ResponseForId(9);
		TEST_ASSERT_NOT_NULL(resp);
		TEST_ASSERT(resp->HasMember("error"));
		TEST_ASSERT_EQ(resp->Member("error").Member("code").GetInt(), (int64)-32601);
		TEST_PASSED();
	}

	void LspServerTest::TestFeatureBeforeInitializeIsRejected()
	{
		Reset("file:///root"); // no initialize sent

		Value params = Value::MakeObject();
		params.SetMember("textDocument", Value::Null());
		params.SetMember("position", Value::Null());
		FeedRequest("textDocument/hover", 5, params);

		const Value* resp = ResponseForId(5);
		TEST_ASSERT_NOT_NULL(resp);
		TEST_ASSERT(resp->HasMember("error"));
		TEST_ASSERT_EQ(resp->Member("error").Member("code").GetInt(), (int64)-32601);
		TEST_PASSED();
	}

	void LspServerTest::TestShutdownThenExitTerminatesCleanly()
	{
		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());

		FeedRequest("shutdown", 7, Value::Null());
		const Value* shutdownResp = ResponseForId(7);
		TEST_ASSERT_NOT_NULL(shutdownResp);
		TEST_ASSERT(shutdownResp->HasMember("result")); // null result, no error

		FeedNotification("exit", Value::Null());
		TEST_ASSERT(m_Server->ShouldExit());
		TEST_ASSERT_EQ(m_Server->ExitCode(), (int)0);
		TEST_PASSED();
	}

	// --- initializationOptions: the project generator passes the compiler's -I dirs and defines so LSP diagnostics match a real build.

	static void WriteFile(const fs::path& path, const std::string& text)
	{
		fs::create_directories(path.parent_path());
		std::ofstream out(path);
		out << text;
	}

	void LspServerTest::TestInitializationOptionsIncludePathsResolveIncludes()
	{
			const fs::path root = fs::temp_directory_path() / "c4lsp_initopts_include" / "src";
		fs::remove_all(root.parent_path()); // deterministic fixture (a crashed prior run must not leak state)
		WriteFile(root / "main.cf", R"(include "util.ch"

int32 main() {
	return util_Sum();
}
)");
		// The header lives OUTSIDE the registered root, exactly like a nuke linked module: its src folder
		// arrives via -I. Inside the root it would resolve without any include path and this test proves nothing.
		WriteFile(root.parent_path() / "extlib" / "util.ch", R"(public int32 util_Sum() {
return 41;
}
)");

		std::string rootUri = C4LSP::Protocol::DocumentUri::ToUri(root.string());
		std::string docUri = C4LSP::Protocol::DocumentUri::ToUri((root / "main.cf").string());

		// A real disk root registers every file under it, so the recompile publishes a diagnostics
		// notification per document: scan ALL of them for the one addressed to this uri.
		auto findDiagnostic = [this](const std::string& uri, const std::function<bool(const Value&)>& match) -> bool {
			size_t checked = 0;
			while (true) {
				const Value* pub = NotificationByMethod("textDocument/publishDiagnostics", &checked);
				if (!pub)
					return false;
				if (pub->Member("params").Member("uri").GetString() != uri)
					continue;
				Value diags = pub->Member("params").Member("diagnostics");
				for (uint32 i = 0; i < diags.Size(); ++i)
					if (match(diags.At(i)))
						return true;
			}
		};

		auto hasIncludeNotFound = [](const Value& d) { return Contains(d.Member("message").GetString(), "include not found"); };

		// 1. Without the include dir, `include "util.ch"` cannot resolve: an error must publish (the real
		//    compiler reports exactly this for a header outside every input/-I folder).
		Reset(rootUri);
		Value initNoOpts = Value::MakeObject();
		initNoOpts.SetMember("rootUri", Value::String(Core::String(rootUri)));
		FeedRequest("initialize", 1, initNoOpts);
		FeedNotification("initialized", Value::Null());
		m_Server->WaitForCurrentWork(); // settle the initialized recompile before didOpen posts another
		FeedNotification("textDocument/didOpen", MakeTextDocParams(docUri, R"(include "util.ch"

int32 main() {
	return util_Sum();
}
)"));
		m_Server->WaitForCurrentWork();
		TEST_ASSERT(findDiagnostic(docUri, hasIncludeNotFound)); // baseline: the include is unresolvable

		// 2. With initializationOptions.includePaths pointing at <root>/../extlib, every diagnostic clears:
		//    the header resolves and its symbols are visible, exactly like a real `-I extlib` build. A fresh
		//    server models the real client session (one initialize per process); re-initializing the same
		//    instance would leave phase 1's open document in m_Documents and muddy the assertion.
		Reset(rootUri);
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String(rootUri)));
		Value options = Value::MakeObject();
		Value pathsArr = Value::MakeArray();
		pathsArr.Append(Value::String(Core::String((root.parent_path() / "extlib").string())));
		options.SetMember("includePaths", pathsArr);
		init.SetMember("initializationOptions", options);
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		m_Server->WaitForCurrentWork(); // settle the initialized recompile before didOpen posts another
		FeedNotification("textDocument/didOpen", MakeTextDocParams(docUri, R"(include "util.ch"

int32 main() {
	return util_Sum();
}
)"));
		m_Server->WaitForCurrentWork();
		TEST_ASSERT(!findDiagnostic(docUri, hasIncludeNotFound)); // resolved via the -I dir
		auto anyError = [](const Value& d) { return d.Member("severity").GetInt() == 1; };
		TEST_ASSERT(!findDiagnostic(docUri, anyError));

		fs::remove_all(root.parent_path());
		TEST_PASSED();
	}

	// BUG-301: without a build description the workspace folder is not compiled whole - it can hold any number of
	// programs - only the files beside the open document (here `a/`, not the other program in `b/`).
	void LspServerTest::TestWithoutADescriptionOnlyTheOpenDocumentsFolderCompiles()
	{
		const fs::path root = fs::temp_directory_path() / "c4lsp_loose_sources";
		fs::remove_all(root);
		WriteFile(root / "a" / "main.cf", "include \"util.ch\"\n\nint32 main() { return Util(); }\n");
		WriteFile(root / "a" / "util.ch", "public int32 Util() { return 1; }\n");
		WriteFile(root / "b" / "main.cf", "int32 main() { return 2; }\n");
		const std::string rootUri = C4LSP::Protocol::DocumentUri::ToUri(root.string());
		const std::string docUri = C4LSP::Protocol::DocumentUri::ToUri((root / "a" / "main.cf").string());
		const std::string otherUri = C4LSP::Protocol::DocumentUri::ToUri((root / "b" / "main.cf").string());

		Reset(rootUri);
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String(rootUri)));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		m_Server->WaitForCurrentWork();
		FeedNotification("textDocument/didOpen", MakeTextDocParams(docUri, "include \"util.ch\"\n\nint32 main() { return Util(); }\n"));
		m_Server->WaitForCurrentWork();

		size_t checked = 0;
		bool sawOther = false;
		bool sawErrorHere = false;
		while (const Value* pub = NotificationByMethod("textDocument/publishDiagnostics", &checked))
		{
			const std::string uri = pub->Member("params").Member("uri").GetString();
			sawOther = sawOther || uri == otherUri;
			if (uri == docUri)
				sawErrorHere = sawErrorHere || pub->Member("params").Member("diagnostics").Size() > 0;
		}
		TEST_ASSERT(!sawOther);     // b/ is another program, never compiled with this one
		TEST_ASSERT(!sawErrorHere); // util.ch beside it resolves, and no second main clashes
		fs::remove_all(root);
		TEST_PASSED();
	}

	// BUG-198: the default library comes from the install the client names (Nuke writes its C4 install folder as c4Home), like
	// C4.exe's own: a Bedrock include resolves there, and a c4Home without Bedrock leaves it unresolved.
	void LspServerTest::TestInitializationOptionsC4HomeIsTheDefaultLibrary()
	{
		const fs::path root = fs::temp_directory_path() / "c4lsp_initopts_home";
		fs::remove_all(root);
		fs::create_directories(root / "src");
		fs::create_directories(root / "emptyhome");
		const std::string rootUri = C4LSP::Protocol::DocumentUri::ToUri((root / "src").string());
		const std::string docUri = C4LSP::Protocol::DocumentUri::ToUri((root / "src" / "main.cf").string());
		const std::string program = "include \"Container/array.ch\"\nint64 Count() { Core::Array<int32> a; a.Push(1); return a.Count(); }\n";

		auto diagnosticsFor = [this, &rootUri, &docUri, &program](const std::string& home) {
			Reset(rootUri);
			Value init = Value::MakeObject();
			init.SetMember("rootUri", Value::String(Core::String(rootUri)));
			Value options = Value::MakeObject();
			options.SetMember("c4Home", Value::String(Core::String(home)));
			init.SetMember("initializationOptions", options);
			FeedRequest("initialize", 1, init);
			FeedNotification("initialized", Value::Null());
			m_Server->WaitForCurrentWork();
			FeedNotification("textDocument/didOpen", MakeTextDocParams(docUri, program));
			m_Server->WaitForCurrentWork();
			std::string messages;
			size_t checked = 0;
			while (const Value* pub = NotificationByMethod("textDocument/publishDiagnostics", &checked))
			{
				if (pub->Member("params").Member("uri").GetString() != docUri) continue;
				messages.clear(); // the last publish for the document is what the editor shows
				Value diags = pub->Member("params").Member("diagnostics");
				for (uint32 i = 0; i < diags.Size(); ++i) messages += std::string(diags.At(i).Member("message").GetString()) + "\n";
			}
			return messages;
		};

		const std::string install = Scripting::Toolchain::Bedrock().parent_path().string();
		const std::string withLibrary = diagnosticsFor(install);
		TEST_ASSERT_EQ(withLibrary, std::string());
		const std::string withoutLibrary = diagnosticsFor((root / "emptyhome").string());
		TEST_ASSERT(Contains(withoutLibrary, "include not found: 'Container/array.ch'"));

		fs::remove_all(root);
		TEST_PASSED();
	}

	// initializationOptions.scripts: a document in a script folder is analyzed as a script against its host's interface -
	// the host's API resolves, a script's own errors and the rules for scripts are reported - and an interface that does
	// not read says so on the document.
	void LspServerTest::TestInitializationOptionsScriptsCompileAgainstTheirHost()
	{
		const fs::path root = fs::temp_directory_path() / "c4lsp_initopts_scripts";
		fs::remove_all(root);
		fs::create_directories(root / "plugin");
		const Testing::TestHost host = Testing::MakeTestHost({
			{ "api.ch", "namespace Api\n{\n\tint32 Twice(int32 v);\n}\n" },
			{ "api.cf", "include \"api.ch\"\nnamespace Api\n{\n\tint32 Twice(int32 v) { return v * 2; }\n}\nint32 main() { return Api::Twice(1); }\n" } });
		if (!host.index) std::cout << host.diagnostics << std::endl;
		TEST_ASSERT(host.index != nullptr);
		std::string error;
		TEST_ASSERT(Scripting::WriteInterface(host.interface, (root / "host.c4i").string(), &error));
		const std::string rootUri = C4LSP::Protocol::DocumentUri::ToUri(root.string());
		const std::string docUri = C4LSP::Protocol::DocumentUri::ToUri((root / "plugin" / "main.cf").string());

		auto diagnosticsFor = [this, &root, &rootUri, &docUri](const std::string& interfacePath, const std::string& script) {
			Reset(rootUri);
			Value init = Value::MakeObject();
			init.SetMember("rootUri", Value::String(Core::String(rootUri)));
			Value scope = Value::MakeObject();
			Value folders = Value::MakeArray();
			folders.Append(Value::String(Core::String((root / "plugin").string())));
			scope.SetMember("folders", folders);
			scope.SetMember("interface", Value::String(Core::String(interfacePath)));
			Value scripts = Value::MakeArray();
			scripts.Append(scope);
			Value options = Value::MakeObject();
			options.SetMember("scripts", scripts);
			init.SetMember("initializationOptions", options);
			FeedRequest("initialize", 1, init);
			FeedNotification("initialized", Value::Null());
			m_Server->WaitForCurrentWork();
			FeedNotification("textDocument/didOpen", MakeTextDocParams(docUri, script));
			m_Server->WaitForCurrentWork();
			std::string messages;
			size_t checked = 0;
			while (const Value* pub = NotificationByMethod("textDocument/publishDiagnostics", &checked))
			{
				if (pub->Member("params").Member("uri").GetString() != docUri) continue;
				messages.clear(); // the last publish for the document is what the editor shows
				Value diags = pub->Member("params").Member("diagnostics");
				for (uint32 i = 0; i < diags.Size(); ++i) messages += std::string(diags.At(i).Member("message").GetString()) + "\n";
			}
			return messages;
		};

		const std::string interfacePath = (root / "host.c4i").string();
		TEST_ASSERT_EQ(diagnosticsFor(interfacePath, "include \"api.ch\"\nint32 Four() { return Api::Twice(2); }\n"), std::string());
		TEST_ASSERT(Contains(diagnosticsFor(interfacePath, "include \"api.ch\"\nint32 Bad() { return Api::Thrice(2); }\n"), "Thrice"));
		TEST_ASSERT(Contains(diagnosticsFor(interfacePath, "include \"api.ch\"\nnamespace Api\n{\n\tint32 Twice(int32 v) { return v; }\n}\n"),
			"a script cannot define"));
		TEST_ASSERT(Contains(diagnosticsFor((root / "missing.c4i").string(), "int32 Five() { return 5; }\n"), "does not read"));

		fs::remove_all(root);
		TEST_PASSED();
	}

	// The build description (docs/language/15-projects.md) reaches the editor: each file has its project's defines, and
	// an engine file that includes a game header gets the boundary error the compiler reports.
	void LspServerTest::TestInitializationOptionsProjectsDescribeTheBuild()
	{
		const fs::path root = fs::temp_directory_path() / "c4lsp_initopts_projects";
		fs::remove_all(root);
		WriteFile(root / "Engine/src/engine.ch", "int32 EngineValue();\n");
		WriteFile(root / "Engine/src/engine.cf", "include \"engine.ch\"\n#ifdef ENGINE_FAST\nint32 EngineValue() { return 1; }\n#endif\n");
		WriteFile(root / "Game/src/main.cf", "include \"engine.ch\"\nint32 main() { return EngineValue() + GAME_MODE; }\n");
		WriteFile(root / "projects.toml",
			"root = \"Game\"\n"
			"[[project]]\nname = \"Game\"\nsources = [\"Game/src\"]\ndepends = [\"Engine\"]\n[project.defines]\nGAME_MODE = \"2\"\n"
			"[[project]]\nname = \"Engine\"\nsources = [\"Engine/src\"]\n[project.defines]\nENGINE_FAST = \"1\"\n");
		const std::string rootUri = C4LSP::Protocol::DocumentUri::ToUri(root.string());
		const std::string gameUri = C4LSP::Protocol::DocumentUri::ToUri((root / "Game/src/main.cf").string());
		const std::string engineUri = C4LSP::Protocol::DocumentUri::ToUri((root / "Engine/src/engine.cf").string());

		auto messagesFor = [this](const std::string& uri) {
			std::string messages;
			size_t checked = 0;
			while (const Value* pub = NotificationByMethod("textDocument/publishDiagnostics", &checked))
			{
				if (pub->Member("params").Member("uri").GetString() != uri) continue;
				messages.clear();
				Value diags = pub->Member("params").Member("diagnostics");
				for (uint32 i = 0; i < diags.Size(); ++i) messages += std::string(diags.At(i).Member("message").GetString()) + "\n";
			}
			return messages;
		};
		auto open = [this, &rootUri, &root](const std::string& uri, const std::string& text) {
			Reset(rootUri);
			Value init = Value::MakeObject();
			init.SetMember("rootUri", Value::String(Core::String(rootUri)));
			Value options = Value::MakeObject();
			options.SetMember("projects", Value::String(Core::String((root / "projects.toml").generic_string())));
			init.SetMember("initializationOptions", options);
			FeedRequest("initialize", 1, init);
			FeedNotification("initialized", Value::Null());
			m_Server->WaitForCurrentWork();
			FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, text));
			m_Server->WaitForCurrentWork();
		};

		open(gameUri, "include \"engine.ch\"\nint32 main() { return EngineValue() + GAME_MODE; }\n");
		TEST_ASSERT_EQ(messagesFor(gameUri), std::string());
		TEST_ASSERT_EQ(messagesFor(engineUri), std::string());

		WriteFile(root / "Game/src/player.ch", "struct Player { public int32 hp; }\n");
		open(engineUri, "include \"engine.ch\"\ninclude \"player.ch\"\n#ifdef ENGINE_FAST\nint32 EngineValue() { return 1; }\n#endif\n");
		TEST_ASSERT(Contains(messagesFor(engineUri),
			"cannot include 'player.ch': it is a file of project 'Game', and project 'Engine' does not depend on 'Game'"));

		fs::remove_all(root);
		TEST_PASSED();
	}

	void LspServerTest::TestInitializationOptionsDefinesSeedMacroTable()
	{
		const fs::path root = fs::temp_directory_path() / "c4lsp_initopts_defines";
		fs::remove_all(root);

		std::string rootUri = C4LSP::Protocol::DocumentUri::ToUri(root.string());
		std::string docUri = C4LSP::Protocol::DocumentUri::ToUri((root / "main.cf").string());

		auto hasDiagnosticAbout = [this](const std::string& uri, const std::string& needle) -> bool {
			const Value* pub = NotificationByMethod("textDocument/publishDiagnostics");
			if (!pub || pub->Member("params").Member("uri").GetString() != uri)
				return false;
			Value diags = pub->Member("params").Member("diagnostics");
			for (uint32 i = 0; i < diags.Size(); ++i)
				if (Contains(diags.At(i).Member("message").GetString(), needle))
					return true;
			return false;
		};

		const std::string program = R"(#if defined(NUKE_FEATURE_X)
int32 g_enabled = 1;
#else
int32 g_disabled = 1;
#endif

int32 main() {
	return g_enabled + g_missing_symbol;
}
)";
		// The preprocess phase (macro expansion, where #if branches are selected) only runs when the root is a
		// real directory with files on disk - mirror a real project by writing the document to its disk location.
		WriteFile(root / "main.cf", program);

		auto feedWithDefines = [this](const std::string& rootUri, const std::string& docUri, const std::string& program, bool withDefine) {
			// A fresh server per phase models the real client session (one initialize per process); it also
			// keeps each phase's diagnostics isolated from the other.
			Reset(rootUri);
			Value init = Value::MakeObject();
			init.SetMember("rootUri", Value::String(Core::String(rootUri)));
			if (withDefine) {
				Value options = Value::MakeObject();
				Value definesObj = Value::MakeObject();
				definesObj.SetMember("NUKE_FEATURE_X", Value::String(Core::String("1")));
				options.SetMember("defines", definesObj);
				init.SetMember("initializationOptions", options);
			}
			FeedRequest("initialize", 1, init);
			FeedNotification("initialized", Value::Null());
			m_Server->WaitForCurrentWork(); // settle the initialized recompile before didOpen posts another
			FeedNotification("textDocument/didOpen", MakeTextDocParams(docUri, program));
			m_Server->WaitForCurrentWork();
		};

		// Without the define: the #else branch is active (g_disabled), so referencing g_enabled errors.
		feedWithDefines(rootUri, docUri, program, false);
		TEST_ASSERT(hasDiagnosticAbout(docUri, "g_enabled")); // undefined without the macro

		// With defines.NUKE_FEATURE_X=1: the #if branch is active and g_enabled resolves - the document is clean.
		feedWithDefines(rootUri, docUri, program, true);
		TEST_ASSERT(!hasDiagnosticAbout(docUri, "g_enabled")); // defined by the injected macro
		TEST_ASSERT(hasDiagnosticAbout(docUri, "g_missing_symbol")); // unrelated planted error still reported

		fs::remove_all(root);
		TEST_PASSED();
	}

	void LspServerTest::TestMalformedInitializationOptionsNeverBreakServer()
	{
		const fs::path root = fs::temp_directory_path() / "c4lsp_initopts_malformed";
		fs::remove_all(root);
		WriteFile(root / "main.cf", "int32 g_ok = 1;\n");

		std::string rootUri = C4LSP::Protocol::DocumentUri::ToUri(root.string());
		std::string docUri = C4LSP::Protocol::DocumentUri::ToUri((root / "main.cf").string());

		auto feedMalformedOptions = [this, &docUri](const std::string& rootUri, const Value& options) {
			m_Messages.clear();
			Value init = Value::MakeObject();
			init.SetMember("rootUri", Value::String(Core::String(rootUri)));
			init.SetMember("initializationOptions", options);
			FeedRequest("initialize", 1, init);
			const Value* resp = ResponseForId(1); // initialize must answer even with garbage options
			TEST_ASSERT_NOT_NULL(resp);
			TEST_ASSERT(!resp->HasMember("error"));
			FeedNotification("initialized", Value::Null());
			FeedNotification("textDocument/didOpen", MakeTextDocParams(docUri, "int32 g_ok = 1;\n"));
			m_Server->WaitForCurrentWork(); // must recompile without crashing/hanging
		};

		Value optsA = Value::MakeArray();                  // options as an array (not object)
		feedMalformedOptions(rootUri, optsA);

		Value optsB = Value::MakeObject();                 // includePaths with non-string entries + defines not an object
		optsB.SetMember("includePaths", Value::String(Core::String("/not/an/array")));
		optsB.SetMember("defines", Value::Int(7));
		feedMalformedOptions(rootUri, optsB);

		Value optsC = Value::MakeObject();                 // defines with non-string values (ignored per entry)
		Value definesObj = Value::MakeObject();
		definesObj.SetMember("BAD", Value::Int(3));
		optsC.SetMember("defines", definesObj);
		feedMalformedOptions(rootUri, optsC);

		fs::remove_all(root);
		TEST_PASSED();
	}

	void LspServerTest::TestFormattingCapabilityAndRequest()
	{
		const std::string program = "void Run(){\n     int x   = 1+2;\n}\n"; // needs: `{`->space, `x = 1 + 2`, indent normalized to exactly 4 spaces

		std::string uri = Testing::Lsp::UriFor("FmtDoc.cf");
		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);

		// The capability must advertise both formatting providers.
		const Value* initResp = ResponseForId(1);
		TEST_ASSERT_NOT_NULL(initResp);
		const Value& caps = initResp->Member("result").Member("capabilities");
		TEST_ASSERT_EQ(caps.Member("documentFormattingProvider").GetBool(), true);
		TEST_ASSERT_EQ(caps.Member("documentRangeFormattingProvider").GetBool(), true);

		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork(); // let the async recompile swap in a built snapshot

		Value params = Value::MakeObject();
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(uri)));
		params.SetMember("textDocument", doc);
		Value options = Value::MakeObject();
		options.SetMember("tabSize", Value::Int(4));
		params.SetMember("options", options);

		FeedRequest("textDocument/formatting", 9, params);

		const Value* resp = ResponseForId(9);
		TEST_ASSERT_NOT_NULL(resp);
		Value result = resp->Member("result");
		TEST_ASSERT(result.IsArray()); // a textEdits array (possibly empty)
		TEST_ASSERT(result.Size() > 0u); // this fixture definitely needs edits

		// Reconstruct the formatted document by applying each returned edit left-to-right. This is
		// the real end-to-end check: JSON serialization, handler wiring and the formatter engine all
		// have to agree on LSP coordinates for the client's applyEdits to work.
		std::vector<std::tuple<int, int, int, int, std::string>> edits; // (startLine, startChar, endLine, endChar, newText)
		for (uint32 i = 0; i < result.Size(); ++i) {
			const Value& e = result.At(i);
			TEST_ASSERT(e.HasMember("range") && e.HasMember("newText"));
			const Value& range = e.Member("range");
			int sLine = (int)range.Member("start").Member("line").GetInt(0);
			int sChar = (int)range.Member("start").Member("character").GetInt(0);
			int eLine = (int)range.Member("end").Member("line").GetInt(0);
			int eChar = (int)range.Member("end").Member("character").GetInt(0);
			edits.emplace_back(sLine, sChar, eLine, eChar, e.Member("newText").GetString());
		}

		std::vector<std::string> lines; // one entry per 0-based line index
		size_t pos = 0;
		while (true) { size_t nl = program.find('\n', pos); if (nl == std::string::npos) break; lines.push_back(program.substr(pos, nl - pos)); pos = nl + 1; }
		lines.push_back(program.substr(pos));

		std::sort(edits.begin(), edits.end()); // lexicographic: line, character first
		std::ostringstream out;
		size_t ei = 0; // monotonic index so zero-width edits can't re-match forever
		for (size_t li = 0; li < lines.size(); ++li) {
			std::string line = lines[li];
			size_t cursor = 0;
			while (ei < edits.size() && std::get<0>(edits[ei]) == (int)li) {
				const auto& e = edits[ei++]; // consume this edit either way
				size_t startCol = std::min<size_t>((size_t)std::get<1>(e), line.size());
				// Insertions don't move the cursor in ORIGINAL coordinates, so a later zero-width
				// edit at an original column beyond the cursor is legitimate and must not be clamped.
				size_t s = (startCol >= cursor) ? startCol : cursor; // clamp only genuine overlap violations
				out << line.substr(cursor, s - cursor);
				cursor = s;
				if (std::get<2>(e) != (int)li) { out << std::get<4>(e); break; } // spans to EOL: newText consumes the rest of this line
				size_t en = std::min<size_t>((size_t)std::get<3>(e), line.size());
				out << std::get<4>(e);
				cursor = en;
			}
			if (li + 1 < lines.size()) out << line.substr(cursor) << '\n';
			else out << line.substr(cursor);
		}

		const std::string formatted = out.str();
		TEST_ASSERT(Testing::Lsp::Contains(formatted, "void Run() {"));     // space before the opening brace... 
		TEST_ASSERT(Testing::Lsp::Contains(formatted, "    int x = 1 + 2;"));// ...indent normalized and operators spaced

		auto stripWs = [](std::string s) { std::string r; for (char c : s) if (!isspace((unsigned char)c)) r += c; return r; };
		TEST_ASSERT(stripWs(formatted) == stripWs(program)); // token text untouched: whitespace-only change
		TEST_PASSED();
	}

	void LspServerTest::TestTidyFindingsPublishWithCompilerDiagnosticsWhenEnabled()
	{
		const std::string program = "int32 g_sink;\nvoid Run()\n{\n\tint32 unused_var;\n}\n"; // valid code with one tidy-only finding
		std::string uri = Testing::Lsp::UriFor("TidyDoc.cf");

		C4LSP::Features::Tidy::SetEnabled(true); // deterministic start (server-side default is now OFF)

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		Value options = Value::MakeObject();
		options.SetMember("tidy", Value::Bool(true)); // what the client sends when the plugin's feature-tidy is on (default)
		init.SetMember("initializationOptions", options);
		FeedRequest("initialize", 1, init); // tidy enabled via initializationOptions at startup
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork();

		auto hasTidyFinding = [this](const std::string& uri) -> bool {
			size_t checked = 0;
			while (true) {
				const Value* pub = NotificationByMethod("textDocument/publishDiagnostics", &checked);
				if (!pub) return false; // no more publish frames: the finding is not there
				if (pub->Member("params").Member("uri").GetString() != uri) continue;
				Value diags = pub->Member("params").Member("diagnostics");
				for (uint32 i = 0; i < diags.Size(); ++i) {
					const Value& d = diags.At(i);
					if (!d.HasMember("source")) continue; // compiler diagnostics carry no source member
					if (Contains(d.Member("source").GetString(), "c4-tidy") && Contains(d.Member("message").GetString(), "unused local variable 'unused_var'"))
						return true;
				}
			}
		};

		TEST_ASSERT(hasTidyFinding(uri)); // the unused-local finding rides the SAME publishDiagnostics as compiler errors
		C4LSP::Features::Tidy::SetEnabled(true);
		TEST_PASSED();
	}

	void LspServerTest::TestDidChangeConfigurationTogglesTidyLive()
	{
		const std::string program = "int32 g_sink;\nvoid Run()\n{\n\tint32 unused_var;\n}\n"; // valid code with one tidy-only finding
		std::string uri = Testing::Lsp::UriFor("TidyToggleDoc.cf");

		C4LSP::Features::Tidy::SetEnabled(false); // start from the off state so the test is deterministic regardless of order

		auto feedConfig = [this](bool tidy) {
			Value settings = Value::MakeObject();
			settings.SetMember("tidy", Value::Bool(tidy));
			Value params = Value::MakeObject();
			params.SetMember("settings", settings);
			FeedNotification("workspace/didChangeConfiguration", params);
		};

		auto hasTidyFinding = [this](const std::string& uri) -> bool {
			size_t checked = 0;
			while (true) {
				const Value* pub = NotificationByMethod("textDocument/publishDiagnostics", &checked);
				if (!pub || pub->Member("params").Member("uri").GetString() != uri) break; // no more frames: not there
				Value diags = pub->Member("params").Member("diagnostics");
				for (uint32 i = 0; i < diags.Size(); ++i) {
					const Value& d = diags.At(i);
					if (!d.HasMember("source")) continue; // compiler diagnostics carry no source member
					if (Contains(d.Member("source").GetString(), "c4-tidy") && Contains(d.Member("message").GetString(), "unused local variable 'unused_var'"))
						return true;
				}
			}
			return false;
		};

		// Phase 1: initialize with tidy OFF (what the client sends when the user has disabled the setting).
		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		Value options = Value::MakeObject();
		options.SetMember("tidy", Value::Bool(false));
		init.SetMember("initializationOptions", options);
		FeedRequest("initialize", 1, init);
		TEST_ASSERT_EQ(C4LSP::Features::Tidy::IsEnabled(), false); // the flag honored initializationOptions

		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork();
		TEST_ASSERT(!hasTidyFinding(uri)); // off at startup: no tidy findings published

		// Phase 2: the user flips the setting in the editor's settings menu. The host pushes the effective
		// profile settings via didChangeConfiguration WITHOUT restarting the server; the live republish
		// must add the finding immediately (no recompile, no document change).
		feedConfig(true); // synchronous handler: the republish is already framed by the time this returns
		TEST_ASSERT(hasTidyFinding(uri));

		// Phase 3: flip back off - the live republish clears it again.
		feedConfig(false);
		C4LSP::Features::Tidy::SetEnabled(true); // leave enabled for other tests (server-side default is OFF)
		TEST_PASSED();
	}

	void LspServerTest::TestSemanticTokensStableAcrossPreprocessorDirectives()
	{
		const fs::path root = fs::temp_directory_path() / "c4lsp_semantics_macros";
		fs::remove_all(root); // deterministic fixture (a crashed prior run must not leak state)

		// CRLF line endings + a #define ABOVE the declarations: MacroPass blanks the directive line and
		// normalizes CRLF->LF before parsing, so every byte offset after it differs between the editor
		// buffer and the effective text the AST was built from. Classification must key on (line, col),
		// which survives that transformation; a byte-offset join lands tokens on neighbors' entries.
		const std::string program =
			"#define SCALE_FACTOR 2\r\n"
			"\r\n"
			"public int32 Compute(int32 x) {\r\n"
			"\tint32 result = x * SCALE_FACTOR;\r\n"
			"\treturn result + 10;\r\n"
			"}\r\n";

		fs::create_directories(root);
		{
			std::ofstream out(root / "main.cf", std::ios::binary); // binary: keep the exact CRLF bytes on disk
			out << program;
		}

		std::string rootUri = C4LSP::Protocol::DocumentUri::ToUri(root.string());
		std::string docUri = C4LSP::Protocol::DocumentUri::ToUri((root / "main.cf").string());

		Reset(rootUri);
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String(rootUri)));
		FeedRequest("initialize", 1, init);
		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(docUri, program));
		m_Server->WaitForCurrentWork();

		Value params = Value::MakeObject();
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(docUri)));
		params.SetMember("textDocument", doc);
		FeedRequest("textDocument/semanticTokens/full", 77, params);

		const Value* resp = ResponseForId(77);
		TEST_ASSERT_NOT_NULL(resp);
		Value data = resp->Member("result").Member("data");
		TEST_ASSERT(data.IsArray());

		// Decode the LSP delta encoding and return (kindId, modifiers) for a token starting at an exact
		// 0-based editor position. Returns false when no token starts there. Per the LSP spec: deltaLine is
		// relative to the previous token; on a NEW line startCharacter is ABSOLUTE within that line, while on
		// the SAME line it is relative to the PREVIOUS token's start character.
		auto findToken = [&data](uint32 wantLine, uint32 wantCol, uint32& kindOut, uint32& modsOut) -> bool {
			uint32 line = 0;
			uint32 lastStartChar = 0;
			for (uint32 i = 0; i + 4 < data.Size(); i += 5) {
				uint32 deltaLine = static_cast<uint32>(data.At(i).GetInt(0));
				uint32 deltaChar = static_cast<uint32>(data.At(i + 1).GetInt(0));
				line += deltaLine;
				uint32 col = (deltaLine == 0) ? lastStartChar + deltaChar : deltaChar;
				if (line == wantLine && col == wantCol) {
					kindOut = static_cast<uint32>(data.At(i + 3).GetInt(0));
					modsOut = static_cast<uint32>(data.At(i + 4).GetInt(0));
					return true;
				}
				lastStartChar = col;
			}
			return false;
		};

		uint32 kind = 0;
		uint32 mods = 0;

		// `public` access modifier on the signature line (editor line 3 -> 0-based 2, col 0). House style:
		// access modifiers are ordinary blue keywords, not the purple modifier family.
		TEST_ASSERT(findToken(2, 0, kind, mods));
		TEST_ASSERT_EQ((int64)kind, (int64)14);    // Keyword

		// `int32` on the signature line (col 8 -> 0-based 7): a built-in type reference.
		TEST_ASSERT(findToken(2, 7, kind, mods));
		TEST_ASSERT_EQ((int64)kind, (int64)1);     // Type
		TEST_ASSERT_EQ((int64)mods, (int64)32);    // Builtin

		// `Compute` free-function definition: Function + Definition.
		TEST_ASSERT(findToken(2, 13, kind, mods));
		TEST_ASSERT_EQ((int64)kind, (int64)10);    // Function
		TEST_ASSERT_EQ((int64)mods, (int64)2);     // Definition

		// `x` parameter on the signature line (col 27). House style: parameters are not a distinct color
		// from variables, so it classifies as Variable like any other local name.
		TEST_ASSERT(findToken(2, 27, kind, mods));
		TEST_ASSERT_EQ((int64)kind, (int64)8);     // Variable

		// `result` local variable inside the body (tab-indented; a tab counts as one column):
		// Variable + Definition. With byte-offset joining this token instead lands on a neighbor's
		// entry from the macro-shifted effective text and fails these assertions.
		TEST_ASSERT(findToken(3, 7, kind, mods));
		TEST_ASSERT_EQ((int64)kind, (int64)8);     // Variable
		TEST_ASSERT_EQ((int64)mods, (int64)2);     // Definition

		fs::remove_all(root);
		TEST_PASSED();
	}

	void LspServerTest::TestCodeActionQuickfixReplacesDotOnPointer()
	{
		const std::string program =
			"struct Point { int32 x; int32 y; }\n"
			"int32 main()\n"
			"{\n"
			"\tPoint p;\n"
			"\tp.x = 1;\n"
			"\tPoint* q = &p;\n"
			"\tq.y = 2; // dot on a pointer: the quickfix must replace exactly this '.' with '->'\n"
			"\treturn 0;\n"
			"}\n";
		std::string uri = Testing::Lsp::UriFor("QuickfixDoc.cf");

		C4LSP::Features::CodeActions::SetQuickfixesEnabled(true); // deterministic start (server default is on)
		C4LSP::Features::CodeActions::SetCreateDefinitionEnabled(false); // isolate the quickfix family in this test

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);

		const Value* initResp = ResponseForId(1);
		TEST_ASSERT_NOT_NULL(initResp);
		TEST_ASSERT_EQ(initResp->Member("result").Member("capabilities").Member("codeActionProvider").GetBool(), true);

		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork(); // the dot-on-pointer error must be diagnosed first

		Value params = Value::MakeObject();
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(uri)));
		params.SetMember("textDocument", doc);
		FeedRequest("textDocument/codeAction", 10, params);

		const Value* resp = ResponseForId(10);
		TEST_ASSERT_NOT_NULL(resp);
		Value result = resp->Member("result");
		TEST_ASSERT(result.IsArray());
		TEST_ASSERT_EQ((int64)result.Size(), (int64)1); // exactly the one quickfix, no stubs (family is off)

		const Value& action = result.At(0);
		TEST_ASSERT(Contains(action.Member("title").GetString(), "Use '->'"));
		TEST_ASSERT_EQ(std::string(action.Member("kind").GetString()), std::string("quickfix"));

		// The edit targets the single '.' token on line 6 (0-based), column 2 ("\tq.y": tab=0, q=1, .=2).
		const Value& range = action.Member("edit").Member("changes").Member(uri).At(0).Member("range");
		TEST_ASSERT_EQ((int64)range.Member("start").Member("line").GetInt(), (int64)6);
		TEST_ASSERT_EQ((int64)range.Member("start").Member("character").GetInt(), (int64)2);
		TEST_ASSERT_EQ((int64)range.Member("end").Member("line").GetInt(), (int64)6);
		TEST_ASSERT_EQ((int64)range.Member("end").Member("character").GetInt(), (int64)3);
		TEST_ASSERT_EQ(std::string(action.Member("edit").Member("changes").Member(uri).At(0).Member("newText").GetString()), std::string("->"));

		// Quickfixes carry the squiggle they fix, scoped to that exact range.
		const Value& diag = action.Member("diagnostics").At(0);
		TEST_ASSERT_EQ((int64)diag.Member("range").Member("start").Member("line").GetInt(), (int64)6);
		TEST_ASSERT(Contains(diag.Member("message").GetString(), "Cannot use '.' on a pointer"));

		C4LSP::Features::CodeActions::SetCreateDefinitionEnabled(true); // restore default for other tests
		TEST_PASSED();
	}

	void LspServerTest::TestCodeActionCreateDefinitionInsertsCompilableStub()
	{
		// An undefined call target gets a stub; an undefined name used as an argument never does. A body reports its
		// first failure only, so each case is its own document.
		const std::string program =
			"int32 main()\n"
			"{\n"
			"\tHelper(1);\n"
			"\treturn 0;\n"
			"}\n";
		const std::string argumentProgram =
			"void Use(int32 v) { }\n"
			"int32 main()\n"
			"{\n"
			"\tUse(Missing);\n"
			"\treturn 0;\n"
			"}\n";
		std::string uri = Testing::Lsp::UriFor("CreateDefDoc.cf");

		C4LSP::Features::CodeActions::SetQuickfixesEnabled(false); // isolate the create-definition family in this test
		C4LSP::Features::CodeActions::SetCreateDefinitionEnabled(true);

		auto codeActionsFor = [this](const std::string& documentUri, const std::string& text, int64 id) {
			Reset("file:///root");
			Value init = Value::MakeObject();
			init.SetMember("rootUri", Value::String(Core::String("file:///root")));
			FeedRequest("initialize", 1, init);
			FeedNotification("initialized", Value::Null());
			FeedNotification("textDocument/didOpen", MakeTextDocParams(documentUri, text));
			m_Server->WaitForCurrentWork();
			Value params = Value::MakeObject();
			Value doc = Value::MakeObject();
			doc.SetMember("uri", Value::String(Core::String(documentUri)));
			params.SetMember("textDocument", doc);
			FeedRequest("textDocument/codeAction", id, params);
			const Value* resp = ResponseForId(id);
			return resp ? resp->Member("result") : Value::Null();
		};

		Value argumentResult = codeActionsFor(Testing::Lsp::UriFor("CreateDefArgumentDoc.cf"), argumentProgram, 12);
		TEST_ASSERT(argumentResult.IsArray());
		TEST_ASSERT_EQ((int64)argumentResult.Size(), (int64)0); // `Missing` is a value, not a function to create

		Value result = codeActionsFor(uri, program, 11);
		TEST_ASSERT(result.IsArray());
		TEST_ASSERT_EQ((int64)result.Size(), (int64)1);

		const Value& action = result.At(0);
		TEST_ASSERT(Contains(action.Member("title").GetString(), "Create definition of 'Helper'"));
		// Emitted as a diagnostic-scoped quickfix (not source.*): Monaco's inline lightbulb filters out
		// source-kind actions, so the fix must ride on its own marker to show up at all.
		TEST_ASSERT_EQ(std::string(action.Member("kind").GetString()), std::string("quickfix"));

		// The attached diagnostic carries the PUBLISHED squiggle (line 2 = "\tHelper(Missing);"), not the
		// edit's insertion point - clients scope lightbulb entries to their marker by this range.
		TEST_ASSERT(action.HasMember("diagnostics"));
		const Value& diag = action.Member("diagnostics").At(0);
		TEST_ASSERT_EQ((int64)diag.Member("range").Member("start").Member("line").GetInt(), (int64)2);
		TEST_ASSERT(Contains(diag.Member("message").GetString(), "no function named Helper"));

		// The edit is a zero-width insertion at the top-left of the enclosing definition (line 0, col 0),
		// i.e. BEFORE `int32 main()` - C4 needs declarations before use and does not link nested defs.
		const Value& textEdit = action.Member("edit").Member("changes").Member(uri).At(0);
		TEST_ASSERT_EQ((int64)textEdit.Member("range").Member("start").Member("line").GetInt(), (int64)0);
		TEST_ASSERT_EQ((int64)textEdit.Member("range").Member("start").Member("character").GetInt(), (int64)0);
		TEST_ASSERT_EQ((int64)textEdit.Member("range").Member("end").Member("line").GetInt(), (int64)0);
		TEST_ASSERT_EQ((int64)textEdit.Member("range").Member("end").Member("character").GetInt(), (int64)0);

		const std::string stub = textEdit.Member("newText").GetString();
		TEST_ASSERT(Contains(stub, "void Helper(...)")); // minimal variadic signature: always compiles and links

		// Apply the edit to the original text exactly as a client's applyEdits would (insertion at 0,0)
		// and prove the result is well-formed C4 with the definition above its first use.
		const std::string applied = stub + program;
		TEST_ASSERT(applied.find("void Helper(...)") < applied.find("int32 main()"));

		C4LSP::Features::CodeActions::SetQuickfixesEnabled(true); // restore default for other tests
		TEST_PASSED();
	}

	void LspServerTest::TestCodeActionsToggleOffViaDidChangeConfiguration()
	{
		const std::string program =
			"struct Point { int32 x; int32 y; }\n"
			"int32 main()\n"
			"{\n"
			"\tPoint p;\n"
			"\tPoint* q = &p;\n"
			"\tq.y = 2;\n"
			"\treturn 0;\n"
			"}\n";
		std::string uri = Testing::Lsp::UriFor("CodeActionToggleDoc.cf");

		auto feedConfig = [this](bool quickfixes, bool createDefinition) {
			Value settings = Value::MakeObject();
			settings.SetMember("quickfixes", Value::Bool(quickfixes));
			settings.SetMember("createDefinition", Value::Bool(createDefinition));
			Value params = Value::MakeObject();
			params.SetMember("settings", settings);
			FeedNotification("workspace/didChangeConfiguration", params); // synchronous: flags flip in place
		};

		auto actionCount = [this](std::string_view uri, int64 id) -> int {
			Value params = Value::MakeObject();
			Value doc = Value::MakeObject();
			doc.SetMember("uri", Value::String(Core::String(std::string(uri))));
			params.SetMember("textDocument", doc);
			FeedRequest("textDocument/codeAction", id, params);

			const Value* resp = ResponseForId(id);
			TEST_ASSERT_NOT_NULL(resp);
			Value result = resp->Member("result");
			TEST_ASSERT(result.IsArray());
			return (int)result.Size();
		};

		// Phase 1: initialize with both families OFF (what the client sends when the user has disabled the
		// settings). The response is an empty array - identical to a server without the capability.
		C4LSP::Features::CodeActions::SetQuickfixesEnabled(true); // force the ON default so init options must win
		C4LSP::Features::CodeActions::SetCreateDefinitionEnabled(true);

		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		Value options = Value::MakeObject();
		options.SetMember("quickfixes", Value::Bool(false));
		options.SetMember("createDefinition", Value::Bool(false));
		init.SetMember("initializationOptions", options);
		FeedRequest("initialize", 1, init);
		TEST_ASSERT_EQ(C4LSP::Features::CodeActions::QuickfixesEnabled(), false); // flags honored initializationOptions
		TEST_ASSERT_EQ(C4LSP::Features::CodeActions::CreateDefinitionEnabled(), false);

		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork();
		TEST_ASSERT_EQ(actionCount(uri, 20), 0); // off at startup: no actions even though the error exists

		// Phase 2: the user flips both settings in the editor's settings menu. The host pushes them live via
		// didChangeConfiguration WITHOUT restarting the server; the next request must see the new flags.
		feedConfig(true, true);
		TEST_ASSERT_EQ(actionCount(uri, 21), 1); // the dot-on-pointer quickfix appears

		// Phase 3: flip back off - empty again. (Code actions are request-driven, so no republish is needed.)
		feedConfig(false, false);
		TEST_ASSERT_EQ(actionCount(uri, 22), 0);

		C4LSP::Features::CodeActions::SetQuickfixesEnabled(true); // restore defaults for other tests
		C4LSP::Features::CodeActions::SetCreateDefinitionEnabled(true);
		TEST_PASSED();
	}

	void LspServerTest::TestFoldingRangeResponseUsesLspFieldNames()
	{
		const std::string program = "void Run()\n{\n\tint32 x;\n}\n"; // one real fold (the body) so the array is non-empty

		std::string uri = Testing::Lsp::UriFor("FoldDoc.cf");
		Reset("file:///root");
		Value init = Value::MakeObject();
		init.SetMember("rootUri", Value::String(Core::String("file:///root")));
		FeedRequest("initialize", 1, init);

		const Value* initResp = ResponseForId(1);
		TEST_ASSERT_NOT_NULL(initResp);
		TEST_ASSERT_EQ(initResp->Member("result").Member("capabilities").HasMember("foldingRangeProvider"), true);

		FeedNotification("initialized", Value::Null());
		FeedNotification("textDocument/didOpen", MakeTextDocParams(uri, program));
		m_Server->WaitForCurrentWork(); // let the async recompile swap in a built snapshot

		Value params = Value::MakeObject();
		Value doc = Value::MakeObject();
		doc.SetMember("uri", Value::String(Core::String(uri)));
		params.SetMember("textDocument", doc);
		FeedRequest("textDocument/foldingRange", 2, params);

		const Value* resp = ResponseForId(2);
		TEST_ASSERT_NOT_NULL(resp);
		Value result = resp->Member("result");
		TEST_ASSERT(result.IsArray());
		// The fixture's function body must yield at least one fold; a zero-item array would make the
		// field-name assertions below vacuous.
		TEST_ASSERT(result.Size() > 0u);

		for (uint32 i = 0; i < result.Size(); ++i)
		{
			const Value& item = result.At(i);
			// LSP FoldingRange fields are `start`/`end`. Monaco's folding model reads ONLY those keys:
			// non-standard names like startLine/endLine parse as a valid array but silently produce ZERO
			// folds in the editor (no error anywhere), so this is locked here end-to-end.
			TEST_ASSERT(item.HasMember("start"));
			TEST_ASSERT(item.HasMember("end"));
			TEST_ASSERT(!item.HasMember("startLine"));
			TEST_ASSERT(!item.HasMember("endLine"));
			uint32 start = (uint32)item.Member("start").GetInt();
			uint32 end = (uint32)item.Member("end").GetInt();
			// 0-based editor coordinates, inclusive, and a fold must hide at least one line.
			TEST_ASSERT(end > start);
		}
		TEST_PASSED();
	}
} // namespace Testing
