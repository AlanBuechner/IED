#include "LspInboxTest.h"
#include "LspServerHarness.h"
#include "Server/MessageInbox.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using C4LSP::Server::MessageInbox;
using Json::Value;
using Testing::Lsp::FileUri;
using Testing::Lsp::ServerHarness;
using Testing::Lsp::WriteFile;

namespace Testing
{
	namespace
	{
		const char* kMain = "int32 Value() { return 7; }\n\nint32 main()\n{\n\treturn Value();\n}\n";
		const char* kOther = "int32 Other() { return 1; }\n";

		constexpr int64 kRequestCancelled = -32800;
		constexpr int64 kContentModified = -32801;
		// Longer than an answer from an analysis already made takes, shorter than a request's wait for one (10 s).
		constexpr auto kAtOnce = std::chrono::seconds(3);
		// Longer than an analysis of the scratch file takes.
		constexpr auto kAnalyzed = std::chrono::seconds(30);
		// Long enough for the server to take a request and begin waiting for an analysis.
		constexpr auto kTaken = std::chrono::milliseconds(300);

		// A scratch folder with two loose files, removed when it goes.
		struct Scratch
		{
			fs::path root;
			std::string main, other; // URIs

			explicit Scratch(const std::string& name) : root(fs::temp_directory_path() / name)
			{
				fs::remove_all(root);
				WriteFile(root / "main.cf", kMain);
				WriteFile(root / "other.cf", kOther);
				main = FileUri(root / "main.cf");
				other = FileUri(root / "other.cf");
			}
			~Scratch()
			{
				std::error_code ignored;
				fs::remove_all(root, ignored);
			}
		};

		Value Options()
		{
			Value options = Value::MakeObject();
			options.SetMember("tidy", Value::Bool(false));
			return options;
		}

		Value IdParams(int64 id)
		{
			Value params = Value::MakeObject();
			params.SetMember("id", Value::Int(id));
			return params;
		}

		Value CancelOf(int64 id) { return ServerHarness::NotificationMessage("$/cancelRequest", IdParams(id)); }

		Value DocumentParams(const std::string& uri)
		{
			Value document = Value::MakeObject();
			document.SetMember("uri", Value::String(Core::String(uri)));
			Value params = Value::MakeObject();
			params.SetMember("textDocument", document);
			return params;
		}

		Value ChangeOf(const std::string& uri, const std::string& text, int64 version)
		{
			Value params = DocumentParams(uri);
			Value document = params.Member("textDocument");
			document.SetMember("version", Value::Int(version));
			params.SetMember("textDocument", document);
			Value change = Value::MakeObject();
			change.SetMember("text", Value::String(Core::String(text)));
			Value changes = Value::MakeArray();
			changes.Append(change);
			params.SetMember("contentChanges", changes);
			return ServerHarness::NotificationMessage("textDocument/didChange", params);
		}

		int64 IdOf(const Value& message) { return message.Member("id").GetInt(); }

		int64 ErrorCodeOf(const Value& response) { return response.Member("error").Member("code").GetInt(); }

		// Whether `holds` came true within `most`, looked at every 20 ms.
		bool Within(std::chrono::steady_clock::duration most, const std::function<bool()>& holds)
		{
			const auto until = std::chrono::steady_clock::now() + most;
			while (!holds())
			{
				if (std::chrono::steady_clock::now() > until) return false;
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			return true;
		}

		bool AnsweredWithin(ServerHarness& server, int64 id, std::chrono::steady_clock::duration most)
		{
			return Within(most, [&] { return !server.Response(id).IsNull(); });
		}

		// The server serving `inbox` on a thread of its own, until the inbox closes.
		class Serving
		{
		public:
			Serving(ServerHarness& server, MessageInbox& inbox) : m_Inbox(inbox), m_Thread([&server, &inbox] { server.Server().Serve(inbox); }) {}
			~Serving()
			{
				m_Inbox.Close();
				m_Thread.join();
			}

		private:
			MessageInbox& m_Inbox;
			std::thread m_Thread;
		};

		// main.cf open and analyzed, then edited while the analysis is held: a request about main.cf waits.
		void EditHeld(ServerHarness& server, const Scratch& scratch)
		{
			server.Open(scratch.main, kMain);
			server.Server().HoldAnalysis(true); // as on a program too large to analyze between two requests
			server.Change(scratch.main, std::string(kMain) + "\n// edited\n");
		}

		void OpenAndEditHeld(ServerHarness& server, const Scratch& scratch)
		{
			server.Initialize(Options());
			EditHeld(server, scratch);
		}
	}

	// --- The inbox -------------------------------------------------------------------------------------------------------

	void LspInboxTest::TestMessagesComeInTheOrderTheyArrived()
	{
		MessageInbox inbox;
		inbox.Push(ServerHarness::NotificationMessage("first", Value::Null()));
		inbox.Push(ServerHarness::NotificationMessage("second", Value::Null()));
		inbox.Close();
		const auto first = inbox.Next();
		const auto second = inbox.Next();
		TEST_ASSERT(first && second);
		TEST_ASSERT_EQ(std::string(first->message.Member("method").GetString()), std::string("first"));
		TEST_ASSERT_EQ(std::string(second->message.Member("method").GetString()), std::string("second"));
		TEST_ASSERT(first->at <= second->at);
		TEST_ASSERT(!inbox.Next()); // closed and empty
		TEST_PASSED();
	}

	void LspInboxTest::TestACancellationMarksItsRequestOnArrival()
	{
		MessageInbox inbox;
		TEST_ASSERT(!inbox.IsCancelled(Value::Int(7)));
		inbox.Push(CancelOf(7));
		TEST_ASSERT(inbox.IsCancelled(Value::Int(7))); // before anyone takes the cancellation
		TEST_ASSERT(!inbox.IsCancelled(Value::Int(8)));
		TEST_ASSERT(!inbox.IsCancelled(Value::String(Core::String("7")))); // a string id is another request
		Value named = Value::MakeObject();
		named.SetMember("id", Value::String(Core::String("seven")));
		inbox.Push(ServerHarness::NotificationMessage("$/cancelRequest", named));
		TEST_ASSERT(inbox.IsCancelled(Value::String(Core::String("seven"))));
		inbox.Forget(Value::Int(7));
		TEST_ASSERT(!inbox.IsCancelled(Value::Int(7)));
		inbox.Push(ServerHarness::NotificationMessage("$/cancelRequest", Value::MakeObject())); // no id: marks nothing
		TEST_ASSERT(!inbox.IsCancelled(Value::Null()));
		TEST_PASSED();
	}

	void LspInboxTest::TestAnEditOfADocumentIsSeenUntilTaken()
	{
		MessageInbox inbox;
		inbox.Push(ServerHarness::NotificationMessage("textDocument/didOpen", DocumentParams("file:///a.cf")));
		TEST_ASSERT(!inbox.HasEditOf("file:///a.cf")); // an open is no edit of a text a request was about
		inbox.Push(ChangeOf("file:///a.cf", "text", 2));
		inbox.Push(ServerHarness::NotificationMessage("textDocument/didClose", DocumentParams("file:///b.cf")));
		TEST_ASSERT(inbox.HasEditOf("file:///a.cf"));
		TEST_ASSERT(inbox.HasEditOf("file:///b.cf"));
		TEST_ASSERT(!inbox.HasEditOf("file:///c.cf"));
		inbox.Next();
		inbox.Next(); // the change, taken
		TEST_ASSERT(!inbox.HasEditOf("file:///a.cf"));
		TEST_ASSERT(inbox.HasEditOf("file:///b.cf"));
		TEST_PASSED();
	}

	void LspInboxTest::TestEachArrivalWakesWhoListens()
	{
		MessageInbox inbox;
		int woken = 0;
		inbox.Push(ServerHarness::NotificationMessage("before", Value::Null()));
		inbox.OnArrival([&woken] { ++woken; });
		inbox.Push(ServerHarness::NotificationMessage("one", Value::Null()));
		inbox.Push(CancelOf(1));
		TEST_ASSERT_EQ(woken, 2);
		inbox.Close();
		TEST_ASSERT_EQ(woken, 3); // the end of the input wakes too: nothing more is coming
		inbox.OnArrival({});
		inbox.Push(ServerHarness::NotificationMessage("after", Value::Null()));
		TEST_ASSERT_EQ(woken, 3);
		TEST_PASSED();
	}

	// --- The server, served from an inbox ----------------------------------------------------------------------------------

	void LspInboxTest::TestACancelledRequestAnswersAtOnce()
	{
		const Scratch scratch("c4lsp_inbox_cancelled");
		ServerHarness server(FileUri(scratch.root));
		OpenAndEditHeld(server, scratch);
		MessageInbox inbox;
		const Value hover = server.RequestMessage("textDocument/hover", ServerHarness::At(scratch.main, kMain, "Value();"));
		{
			Serving serving(server, inbox);
			inbox.Push(hover);
			std::this_thread::sleep_for(kTaken); // waiting for the held analysis
			TEST_ASSERT(server.Response(IdOf(hover)).IsNull());
			inbox.Push(CancelOf(IdOf(hover)));
			TEST_ASSERT(AnsweredWithin(server, IdOf(hover), kAtOnce)); // not after the 10 s wait
			TEST_ASSERT_EQ(ErrorCodeOf(server.Response(IdOf(hover))), kRequestCancelled);
		}
		server.Server().HoldAnalysis(false);
		TEST_PASSED();
	}

	void LspInboxTest::TestARequestBeforeAnEditOfItsDocumentAnswersContentModified()
	{
		const Scratch scratch("c4lsp_inbox_modified");
		ServerHarness server(FileUri(scratch.root));
		OpenAndEditHeld(server, scratch);
		MessageInbox inbox;
		const Value hover = server.RequestMessage("textDocument/hover", ServerHarness::At(scratch.main, kMain, "Value();"));
		const Value after = server.RequestMessage("textDocument/hover", ServerHarness::At(scratch.main, kMain, "Value();"));
		{
			Serving serving(server, inbox);
			inbox.Push(hover);
			std::this_thread::sleep_for(kTaken);
			inbox.Push(ChangeOf(scratch.main, std::string(kMain) + "\n// edited again\n", 3));
			TEST_ASSERT(AnsweredWithin(server, IdOf(hover), kAtOnce));
			TEST_ASSERT_EQ(ErrorCodeOf(server.Response(IdOf(hover))), kContentModified);
			inbox.Push(after); // about the text now: answered once it is analyzed
			server.Server().HoldAnalysis(false);
			TEST_ASSERT(AnsweredWithin(server, IdOf(after), kAnalyzed));
			TEST_ASSERT(server.Response(IdOf(after)).Member("result").IsObject());
		}
		TEST_PASSED();
	}

	void LspInboxTest::TestAnEditOfAnotherDocumentLeavesARequestWaiting()
	{
		const Scratch scratch("c4lsp_inbox_other");
		ServerHarness server(FileUri(scratch.root));
		server.Initialize(Options());
		server.Open(scratch.other, kOther);
		EditHeld(server, scratch);
		MessageInbox inbox;
		const Value hover = server.RequestMessage("textDocument/hover", ServerHarness::At(scratch.main, kMain, "Value();"));
		{
			Serving serving(server, inbox);
			inbox.Push(hover);
			std::this_thread::sleep_for(kTaken);
			inbox.Push(ChangeOf(scratch.other, std::string(kOther) + "\n", 2));
			std::this_thread::sleep_for(kTaken);
			TEST_ASSERT(server.Response(IdOf(hover)).IsNull()); // its own text did not change: it still waits
			server.Server().HoldAnalysis(false);
			TEST_ASSERT(AnsweredWithin(server, IdOf(hover), kAnalyzed));
			TEST_ASSERT(server.Response(IdOf(hover)).Member("result").IsObject());
		}
		TEST_PASSED();
	}

	void LspInboxTest::TestARequestsBudgetCountsFromItsArrival()
	{
		const Scratch scratch("c4lsp_inbox_budget");
		ServerHarness server(FileUri(scratch.root));
		OpenAndEditHeld(server, scratch);
		const Value hover = server.RequestMessage("textDocument/hover", ServerHarness::At(scratch.main, kMain, "Value();"));
		const auto start = std::chrono::steady_clock::now();
		server.Server().HandleMessage(hover, start - std::chrono::seconds(30)); // it waited behind others past its budget
		const auto took = std::chrono::steady_clock::now() - start;
		server.Server().HoldAnalysis(false);
		TEST_ASSERT(took < kAtOnce); // the analysis there is answers, not another wait
		TEST_ASSERT(server.Response(IdOf(hover)).Member("result").IsObject());
		TEST_PASSED();
	}

	void LspInboxTest::TestAnAnsweredRequestsCancellationIsForgotten()
	{
		const Scratch scratch("c4lsp_inbox_forgotten");
		ServerHarness server(FileUri(scratch.root));
		server.Initialize(Options());
		server.Open(scratch.main, kMain);
		MessageInbox inbox;
		const Value hover = server.RequestMessage("textDocument/hover", ServerHarness::At(scratch.main, kMain, "Value();"));
		{
			Serving serving(server, inbox);
			inbox.Push(hover);
			TEST_ASSERT(AnsweredWithin(server, IdOf(hover), kAtOnce));
			TEST_ASSERT(server.Response(IdOf(hover)).Member("result").IsObject());
			inbox.Push(CancelOf(IdOf(hover))); // too late: the client sent it before the answer reached it
			TEST_ASSERT(Within(kAtOnce, [&] { return !inbox.IsCancelled(Value::Int(IdOf(hover))); }));
		}
		TEST_PASSED();
	}

	void LspInboxTest::TestABurstOfTypingCostsOneAnalysis()
	{
		const Scratch scratch("c4lsp_inbox_typing");
		ServerHarness server(FileUri(scratch.root));
		OpenAndEditHeld(server, scratch);
		const size_t before = server.Server().AnalysesMade();
		MessageInbox inbox;
		std::vector<int64> outdated;       // waiting for an analysis, with an edit behind them
		std::vector<int64> outdatedTokens; // answered from the last analysis, edits or not
		std::vector<int64> last;
		{
			Serving serving(server, inbox);
			std::string text = kMain;
			for (int edit = 0; edit < 20; ++edit)
			{
				text += "\n// typed";
				inbox.Push(ChangeOf(scratch.main, text, 10 + edit));
				last.clear();
				for (const char* method : { "textDocument/hover", "textDocument/documentHighlight", "textDocument/semanticTokens/full" })
				{
					const Value request = server.RequestMessage(method, std::string(method) == "textDocument/semanticTokens/full"
						? DocumentParams(scratch.main) : ServerHarness::At(scratch.main, kMain, "Value();"));
					inbox.Push(request);
					last.push_back(IdOf(request));
				}
				if (edit < 19)
				{
					outdated.insert(outdated.end(), last.begin(), last.begin() + 2);
					outdatedTokens.push_back(last[2]);
				}
			}
			// Each outdated request has an edit of its document behind it: answered without a wait, with the analysis held.
			TEST_ASSERT(Within(kAtOnce, [&] {
				for (const std::vector<int64>* ids : { &outdated, &outdatedTokens })
					for (int64 id : *ids)
						if (server.Response(id).IsNull()) return false;
				return true;
			}));
			for (int64 id : outdated) TEST_ASSERT_EQ(ErrorCodeOf(server.Response(id)), kContentModified);
			// Tokens are not dropped for an edit: an error would clear the editor's colors until it asks again.
			for (int64 id : outdatedTokens) TEST_ASSERT(server.Response(id).Member("result").Member("data").IsArray());
			server.Server().HoldAnalysis(false);
			for (int64 id : last) TEST_ASSERT(AnsweredWithin(server, id, kAnalyzed));
			TEST_ASSERT(server.Response(last[0]).Member("result").IsObject());
			TEST_ASSERT(server.Response(last[2]).Member("result").Member("data").IsArray());
		}
		TEST_ASSERT_EQ(server.Server().AnalysesMade(), before + 1); // the last text alone
		TEST_PASSED();
	}

	void LspInboxTest::TestAWaitingRequestHoldsNoOtherBack()
	{
		const Scratch scratch("c4lsp_inbox_parked");
		ServerHarness server(FileUri(scratch.root));
		OpenAndEditHeld(server, scratch);
		MessageInbox inbox;
		const Value hover = server.RequestMessage("textDocument/hover", ServerHarness::At(scratch.main, kMain, "Value();"));
		const Value tokens = server.RequestMessage("textDocument/semanticTokens/full", DocumentParams(scratch.main));
		const Value folding = server.RequestMessage("textDocument/foldingRange", DocumentParams(scratch.main));
		const Value completion = server.RequestMessage("textDocument/completion", ServerHarness::At(scratch.main, kMain, "Value();"));
		{
			Serving serving(server, inbox);
			for (const Value* request : { &hover, &tokens, &folding, &completion }) inbox.Push(*request);
			for (const Value* request : { &tokens, &folding, &completion })
				TEST_ASSERT(AnsweredWithin(server, IdOf(*request), kAtOnce)); // with the analysis held, behind the hover
			TEST_ASSERT(server.Response(IdOf(tokens)).Member("result").Member("data").IsArray());
			TEST_ASSERT(server.Response(IdOf(hover)).IsNull()); // still waiting for the edit's analysis
			server.Server().HoldAnalysis(false);
			TEST_ASSERT(AnsweredWithin(server, IdOf(hover), kAnalyzed));
			TEST_ASSERT(server.Response(IdOf(hover)).Member("result").IsObject());
		}
		TEST_PASSED();
	}

	void LspInboxTest::TestCompletionAnswersWhileTyping()
	{
		const Scratch scratch("c4lsp_inbox_completion");
		ServerHarness server(FileUri(scratch.root));
		server.Initialize(Options());
		server.Open(scratch.main, kMain);
		server.Server().HoldAnalysis(true);
		// A line typed in main since its analysis: `Val` on the line before its return.
		const std::string typed = "int32 Value() { return 7; }\n\nint32 main()\n{\n\tVal\n\treturn Value();\n}\n";
		server.Change(scratch.main, typed);
		MessageInbox inbox;
		const Value completion = server.RequestMessage("textDocument/completion", ServerHarness::At(scratch.main, typed, "\n\treturn"));
		{
			Serving serving(server, inbox);
			inbox.Push(completion);
			inbox.Push(ChangeOf(scratch.main, "int32 Value() { return 7; }\n\nint32 main()\n{\n\tValu\n\treturn Value();\n}\n", 3));
			TEST_ASSERT(AnsweredWithin(server, IdOf(completion), kAtOnce));
			const Value result = server.Response(IdOf(completion)).Member("result");
			TEST_ASSERT(result.IsArray());
			bool offered = false;
			for (uint32 i = 0; i < result.Size(); ++i)
				offered = offered || std::string(result.At(i).Member("label").GetString()) == "Value";
			TEST_ASSERT(offered);
		}
		server.Server().HoldAnalysis(false);
		TEST_PASSED();
	}

	void LspInboxTest::TestAParkedRequestsCancellationAnswersIt()
	{
		const Scratch scratch("c4lsp_inbox_parked_cancel");
		ServerHarness server(FileUri(scratch.root));
		OpenAndEditHeld(server, scratch);
		MessageInbox inbox;
		const Value hover = server.RequestMessage("textDocument/hover", ServerHarness::At(scratch.main, kMain, "Value();"));
		{
			Serving serving(server, inbox);
			inbox.Push(hover);
			std::this_thread::sleep_for(kTaken); // parked
			inbox.Push(CancelOf(IdOf(hover)));
			TEST_ASSERT(AnsweredWithin(server, IdOf(hover), kAtOnce));
			TEST_ASSERT_EQ(ErrorCodeOf(server.Response(IdOf(hover))), kRequestCancelled);
			server.Server().HoldAnalysis(false);
			server.Server().WaitForCurrentWork();
			std::this_thread::sleep_for(kTaken);
			TEST_ASSERT_EQ(server.ResponseCount(IdOf(hover)), size_t(1)); // not answered again once its analysis came
		}
		TEST_PASSED();
	}

	void LspInboxTest::TestServingEndsAtExit()
	{
		const Scratch scratch("c4lsp_inbox_exit");
		ServerHarness server(FileUri(scratch.root));
		server.Initialize(Options());
		MessageInbox inbox;
		std::atomic<bool> served{ false };
		std::thread serving([&] {
			server.Server().Serve(inbox);
			served = true;
		});
		inbox.Push(ServerHarness::NotificationMessage("exit", Value::Null()));
		const bool ended = Within(kAtOnce, [&] { return served.load(); });
		inbox.Close(); // ends it anyway, so the test never hangs
		serving.join();
		TEST_ASSERT(ended);
		TEST_ASSERT(server.Server().ShouldExit());
		TEST_PASSED();
	}
} // namespace Testing
