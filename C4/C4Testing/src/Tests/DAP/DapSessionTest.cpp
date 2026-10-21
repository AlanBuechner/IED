#include "DapSessionTest.h"
#include "DapTestSupport.h"

using namespace Testing::Dap;

namespace Testing
{
	namespace
	{
		const char* const kBasic =
			"int32 g_seed = 3;\n"
			"int32 Twice(int32 value) { // twice-open\n"
			"\tint32 doubled = value * 2; // twice-body\n"
			"\treturn doubled; // twice-return\n"
			"}\n"
			"int32 main() {\n"
			"\tint32 start = g_seed; // main-start\n"
			"\tint32 result = Twice(start); // main-call\n"
			"\tprintf(\"result %d\\n\", result); // main-print\n"
			"\treturn result - 6; // main-return\n"
			"}\n";

		int64 ThreadOf(const Json::Value& stop) { return stop.Member("body").Member("threadId").GetInt(); }
		int64 LineOf(const Json::Value& frame) { return frame.Member("line").GetInt(); }
		std::string NameOf(const Json::Value& frame) { return frame.Member("name").GetString(); }
		std::string ReasonOf(const Json::Value& stop) { return stop.Member("body").Member("reason").GetString(); }

		Json::Value Lines(std::initializer_list<uint32_t> lines)
		{
			Json::Value list = Json::Value::MakeArray();
			for (uint32_t line : lines) list.Append(Object({ { "line", Num(line) } }));
			return list;
		}

		// Steps `thread` and returns the stop the step ends in.
		Json::Value StepAndWait(DapClient& client, const char* kind, int64 thread)
		{
			client.Request(kind, Object({ { "threadId", Num(thread) } }));
			return client.WaitEvent("stopped");
		}
	}

	void DapSessionTest::ABreakpointStopsWithTheStackAndLocals()
	{
		const DebugProgram program = BuildProgram("stack", kBasic);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { Dap::LineOf(kBasic, "twice-body") });
		TEST_ASSERT(!stop.IsNull());
		TEST_ASSERT_EQ(ReasonOf(stop), std::string("breakpoint"));
		TEST_ASSERT_EQ(stop.Member("body").Member("hitBreakpointIds").Size(), 1u);
		const Json::Value frames = client.Request("stackTrace", Object({ { "threadId", Num(ThreadOf(stop)) } })).Member("body").Member("stackFrames");
		TEST_ASSERT_GE(frames.Size(), 2u);
		TEST_ASSERT_EQ(NameOf(frames.At(0)), std::string("Twice"));
		TEST_ASSERT_EQ(LineOf(frames.At(0)), int64(Dap::LineOf(kBasic, "twice-body")));
		TEST_ASSERT(frames.At(0).Member("source").Member("path").GetString().find("main.cf") != std::string::npos);
		TEST_ASSERT_EQ(NameOf(frames.At(1)), std::string("main"));
		TEST_ASSERT_EQ(LineOf(frames.At(1)), int64(Dap::LineOf(kBasic, "main-call")));
		const auto locals = client.Locals(frames.At(0).Member("id").GetInt());
		TEST_ASSERT_EQ(locals.size(), size_t(2));
		TEST_ASSERT_EQ(locals[0].first, std::string("value"));
		TEST_ASSERT_EQ(locals[0].second.Member("value").GetString(), std::string("3"));
		TEST_ASSERT_EQ(locals[0].second.Member("type").GetString(), std::string("int32"));
		TEST_ASSERT_EQ(locals[1].first, std::string("doubled"));
		const auto callerLocals = client.Locals(frames.At(1).Member("id").GetInt()); // a caller's frame reads its own
		TEST_ASSERT_EQ(callerLocals[0].first, std::string("start"));
		TEST_ASSERT_EQ(callerLocals[0].second.Member("value").GetString(), std::string("3"));
		client.Request("continue", Object({ { "threadId", Num(ThreadOf(stop)) } }));
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), int64(0));
		TEST_PASSED();
	}

	void DapSessionTest::StepsLandOnTheRightLines()
	{
		const DebugProgram program = BuildProgram("steps", kBasic);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { Dap::LineOf(kBasic, "main-call") });
		TEST_ASSERT(!stop.IsNull());
		const int64 thread = ThreadOf(stop);
		client.Request("setBreakpoints", Object({ { "source", Object({ { "path", Str(program.source.string()) } }) }, { "breakpoints", Lines({}) } }));
		struct Expected { const char* kind; const char* function; const char* marker; };
		const Expected steps[] = {
			{ "stepIn", "Twice", "twice-open" }, { "next", "Twice", "twice-body" }, { "next", "Twice", "twice-return" },
			{ "next", "main", "main-call" }, { "next", "main", "main-print" }, { "stepIn", "main", "main-return" }, // printf has no lines: run through
		};
		for (const Expected& expected : steps)
		{
			const Json::Value stepped = StepAndWait(client, expected.kind, thread);
			TEST_ASSERT_EQ(ReasonOf(stepped), std::string("step"));
			const Json::Value top = client.TopFrame(thread);
			TEST_ASSERT_EQ(NameOf(top), std::string(expected.function));
			TEST_ASSERT_EQ(LineOf(top), int64(Dap::LineOf(kBasic, expected.marker)));
		}
		client.Request("stepOut", Object({ { "threadId", Num(thread) } })); // out of main: the program runs to its end
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), int64(0));
		TEST_PASSED();
	}

	void DapSessionTest::ABreakpointInsideAStepOverStops()
	{
		const DebugProgram program = BuildProgram("stepbreak", kBasic);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { Dap::LineOf(kBasic, "main-call"), Dap::LineOf(kBasic, "twice-body") });
		TEST_ASSERT_EQ(LineOf(client.TopFrame(ThreadOf(stop))), int64(Dap::LineOf(kBasic, "main-call")));
		const Json::Value stepped = StepAndWait(client, "next", ThreadOf(stop));
		TEST_ASSERT_EQ(ReasonOf(stepped), std::string("breakpoint"));
		TEST_ASSERT_EQ(LineOf(client.TopFrame(ThreadOf(stop))), int64(Dap::LineOf(kBasic, "twice-body")));
		TEST_PASSED();
	}

	void DapSessionTest::ARecursiveCallDoesNotEndAStepEarly()
	{
		const char* const source =
			"int32 Depth(int32 n) {\n"
			"\tif (n == 0) return 0;\n"
			"\tint32 below = Depth(n - 1); // recurse\n"
			"\treturn below + 1; // after\n"
			"}\n"
			"int32 main() { return Depth(3) - 3; }\n";
		const DebugProgram program = BuildProgram("recursion", source);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { Dap::LineOf(source, "recurse") });
		const int64 thread = ThreadOf(stop);
		TEST_ASSERT_EQ(client.Evaluate("n", client.TopFrame(thread).Member("id").GetInt()), std::string("3"));
		client.Request("setBreakpoints", Object({ { "source", Object({ { "path", Str(program.source.string()) } }) }, { "breakpoints", Lines({}) } }));
		StepAndWait(client, "next", thread); // over Depth(2), which reaches the same return address deeper down
		const Json::Value top = client.TopFrame(thread);
		TEST_ASSERT_EQ(LineOf(top), int64(Dap::LineOf(source, "after")));
		TEST_ASSERT_EQ(client.Evaluate("n", top.Member("id").GetInt()), std::string("3"));
		TEST_ASSERT_EQ(client.Evaluate("below", top.Member("id").GetInt()), std::string("2"));
		TEST_PASSED();
	}

	void DapSessionTest::AStepOverAThrowStopsInTheCatch()
	{
		const char* const source =
			"class Oops { public int32 code; }\n"
			"void Fail(int32 code) { Oops o; o.code = code; throw o; }\n"
			"int32 Quiet(int32 code) { try { Fail(code); } catch (Oops e) { return 0; } return 1; }\n"
			"int32 main() {\n"
			"\tint32 result = Quiet(3); // caught-inside\n"
			"\ttry {\n"
			"\t\tFail(5); // throwing-call\n"
			"\t\tresult = 1;\n"
			"\t}\n"
			"\tcatch (Oops e) {\n"
			"\t\tresult = e.code; // catch-body\n"
			"\t}\n"
			"\treturn result - 5; // after\n"
			"}\n";
		const DebugProgram program = BuildProgram("stepthrow", source);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { Dap::LineOf(source, "caught-inside") });
		const int64 thread = ThreadOf(stop);
		client.Request("setBreakpoints", Object({ { "source", Object({ { "path", Str(program.source.string()) } }) }, { "breakpoints", Lines({}) } }));
		// A call that throws and catches inside itself is stepped over as any call (a `try` has no code of its own).
		StepAndWait(client, "next", thread);
		TEST_ASSERT_EQ(LineOf(client.TopFrame(thread)), int64(Dap::LineOf(source, "throwing-call")));
		// One whose throw leaves it ends the step in the catch that takes it.
		const Json::Value caught = StepAndWait(client, "next", thread);
		TEST_ASSERT_EQ(ReasonOf(caught), std::string("step"));
		const Json::Value top = client.TopFrame(thread);
		TEST_ASSERT_EQ(LineOf(top), int64(Dap::LineOf(source, "catch-body")));
		TEST_ASSERT_EQ(NameOf(top), std::string("main"));
		// The catch runs as a funclet of its own; the function's variables are still its.
		TEST_ASSERT_EQ(client.Evaluate("e.code", top.Member("id").GetInt()), std::string("5"));
		TEST_ASSERT_EQ(client.Evaluate("result", top.Member("id").GetInt()), std::string("0"));
		client.Request("continue", Object({ { "threadId", Num(thread) } }));
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), int64(0));
		TEST_PASSED();
	}

	// The first call that may throw past a local with a destructor makes the local's cleanup pad; the cleanup's
	// statements must not lend the call their line, or a breakpoint on it moves to the next line (BUG-307).
	void DapSessionTest::ACallThatFirstNeedsACleanupKeepsItsLine()
	{
		const char* const source =
			"int32 g_deleted = 0;\n"
			"class Holder {\n"
			"\tpublic int32 value;\n"
			"\t[operator(\"delete\")] public void Delete() { g_deleted += 1; }\n"
			"}\n"
			"void Touch(Holder& held) { held.value += 1; }\n"
			"int32 main() {\n"
			"\tHolder held; // held-declared\n"
			"\tTouch(held); // first-call\n"
			"\treturn held.value - 1; // after-call\n"
			"}\n";
		const DebugProgram program = BuildProgram("cleanupline", source);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { Dap::LineOf(source, "first-call") });
		TEST_ASSERT(!stop.IsNull());
		const Json::Value frames = client.Request("stackTrace", Object({ { "threadId", Num(ThreadOf(stop)) } })).Member("body").Member("stackFrames");
		TEST_ASSERT_EQ(LineOf(frames.At(0)), int64(Dap::LineOf(source, "first-call")));
		const Json::Value next = StepAndWait(client, "next", ThreadOf(stop));
		const Json::Value after = client.Request("stackTrace", Object({ { "threadId", Num(ThreadOf(next)) } })).Member("body").Member("stackFrames");
		TEST_ASSERT_EQ(LineOf(after.At(0)), int64(Dap::LineOf(source, "after-call")));
		client.Request("continue", Object({ { "threadId", Num(ThreadOf(next)) } }));
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), int64(0));
		TEST_PASSED();
	}

	void DapSessionTest::ALineWithoutCodeBindsToTheNextLine()
	{
		const char* const source =
			"int32 main() {\n"
			"\t// a comment, no code\n"
			"\n"
			"\tint32 seven = 7; // first-code\n"
			"\treturn seven - 7;\n"
			"}\n";
		const DebugProgram program = BuildProgram("nextline", source);
		TEST_ASSERT(program.built);
		DapClient client;
		client.Request("initialize");
		client.Request("launch", Object({ { "program", Str(program.exe.string()) } }));
		const Json::Value bound = client.Request("setBreakpoints",
			Object({ { "source", Object({ { "path", Str(program.source.string()) } }) }, { "breakpoints", Lines({ 2 }) } }));
		const Json::Value breakpoint = bound.Member("body").Member("breakpoints").At(0);
		TEST_ASSERT(breakpoint.Member("verified").GetBool());
		TEST_ASSERT_EQ(breakpoint.Member("line").GetInt(), int64(Dap::LineOf(source, "first-code")));
		const Json::Value nowhere = client.Request("setBreakpoints",
			Object({ { "source", Object({ { "path", Str(program.source.string() + ".other") } }) }, { "breakpoints", Lines({ 1 }) } }));
		TEST_ASSERT(!nowhere.Member("body").Member("breakpoints").At(0).Member("verified").GetBool(true));
		client.Request("configurationDone");
		const Json::Value stop = client.WaitEvent("stopped");
		TEST_ASSERT_EQ(LineOf(client.TopFrame(ThreadOf(stop))), int64(Dap::LineOf(source, "first-code")));
		TEST_PASSED();
	}

	void DapSessionTest::ATemplateBreakpointStopsInEveryInstance()
	{
		const char* const source =
			"template<T> T Doubled(T v) {\n"
			"\treturn v + v; // template-body\n"
			"}\n"
			"int32 main() {\n"
			"\tint32 i = Doubled<int32>(2);\n"
			"\tfloat f = Doubled<float>(1.5);\n"
			"\treturn i - 4 + (int32)(f - 3.0);\n"
			"}\n";
		const DebugProgram program = BuildProgram("template", source);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value first = client.Start(program, { Dap::LineOf(source, "template-body") });
		TEST_ASSERT_EQ(ReasonOf(first), std::string("breakpoint"));
		const int64 thread = ThreadOf(first);
		const std::string firstValue = client.Evaluate("v", client.TopFrame(thread).Member("id").GetInt());
		client.Request("continue", Object({ { "threadId", Num(thread) } }));
		const Json::Value second = client.WaitEvent("stopped");
		TEST_ASSERT_EQ(ReasonOf(second), std::string("breakpoint"));
		const std::string secondValue = client.Evaluate("v", client.TopFrame(thread).Member("id").GetInt());
		TEST_ASSERT((firstValue == "2" && secondValue == "1.5") || (firstValue == "1.5" && secondValue == "2"));
		client.Request("continue", Object({ { "threadId", Num(thread) } }));
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), int64(0));
		TEST_PASSED();
	}

	void DapSessionTest::ALambdaBodyStops()
	{
		const char* const source =
			"int32 main() {\n"
			"\tfuncptr<int32(int32)> triple = [](int32 x) -> int32 {\n"
			"\t\treturn x * 3; // lambda-body\n"
			"\t};\n"
			"\treturn triple(4) - 12;\n"
			"}\n";
		const DebugProgram program = BuildProgram("lambda", source);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { Dap::LineOf(source, "lambda-body") });
		TEST_ASSERT(!stop.IsNull());
		const Json::Value top = client.TopFrame(ThreadOf(stop));
		TEST_ASSERT_EQ(LineOf(top), int64(Dap::LineOf(source, "lambda-body")));
		TEST_ASSERT_EQ(client.Evaluate("x", top.Member("id").GetInt()), std::string("4"));
		TEST_PASSED();
	}

	void DapSessionTest::StopOnEntryStopsAtMain()
	{
		const DebugProgram program = BuildProgram("entry", kBasic);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, {}, true);
		TEST_ASSERT_EQ(ReasonOf(stop), std::string("entry"));
		const Json::Value top = client.TopFrame(ThreadOf(stop));
		TEST_ASSERT_EQ(NameOf(top), std::string("main"));
		TEST_ASSERT_EQ(LineOf(top), int64(Dap::LineOf(kBasic, "int32 main()")));
		TEST_PASSED();
	}

	void DapSessionTest::OutputAndTheExitCodeReachTheClient()
	{
		const char* const source = "int32 main() {\n\tprintf(\"first line\\nsecond line\\n\");\n\treturn 7;\n}\n";
		const DebugProgram program = BuildProgram("output", source);
		TEST_ASSERT(program.built);
		DapClient client;
		client.Request("initialize");
		TEST_ASSERT(client.Request("launch", Object({ { "program", Str(program.exe.string()) } })).Member("success").GetBool());
		client.Request("configurationDone");
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), int64(7));
		TEST_ASSERT(!client.WaitEvent("terminated").IsNull());
		std::string printed;
		for (const Json::Value& output : client.History("output"))
			if (output.Member("body").Member("category").GetString() == "stdout") printed += output.Member("body").Member("output").GetString();
		TEST_ASSERT(printed.find("first line") != std::string::npos);
		TEST_ASSERT(printed.find("second line") != std::string::npos);
		TEST_PASSED();
	}

	void DapSessionTest::PauseBreaksInAndTerminateEnds()
	{
		const char* const source =
			"int32 Spin(int32 limit) {\n"
			"\tint32 turns = 0;\n"
			"\twhile (turns >= 0) { turns = (turns + 1) % limit; } // spin\n"
			"\treturn turns;\n"
			"}\n"
			"int32 main() { return Spin(1000); }\n";
		const DebugProgram program = BuildProgram("pause", source);
		TEST_ASSERT(program.built);
		DapClient client;
		client.Request("initialize");
		client.Request("launch", Object({ { "program", Str(program.exe.string()) } }));
		client.Request("configurationDone");
		TEST_ASSERT(client.Request("pause", Object({ { "threadId", Num(0) } })).Member("success").GetBool());
		const Json::Value stop = client.WaitEvent("stopped");
		TEST_ASSERT_EQ(ReasonOf(stop), std::string("pause"));
		// The thread a pause injects is not the program's: every thread listed is one the client was told started.
		const Json::Value threads = client.Request("threads").Member("body").Member("threads");
		TEST_ASSERT_GE(threads.Size(), 1u);
		const std::vector<Json::Value> started = client.History("thread");
		for (uint32_t i = 0; i < threads.Size(); ++i)
		{
			bool announced = false;
			for (const Json::Value& event : started)
				announced = announced || event.Member("body").Member("threadId").GetInt() == threads.At(i).Member("id").GetInt();
			TEST_ASSERT(announced);
		}
		const Json::Value frames = client.Request("stackTrace", Object({ { "threadId", Num(ThreadOf(stop)) } })).Member("body").Member("stackFrames");
		bool inSpin = false;
		for (uint32_t i = 0; i < frames.Size(); ++i) inSpin = inSpin || NameOf(frames.At(i)) == "Spin";
		TEST_ASSERT(inSpin);
		TEST_ASSERT(client.Request("terminate").Member("success").GetBool());
		TEST_ASSERT(!client.WaitEvent("exited").IsNull());
		TEST_ASSERT(!client.WaitEvent("terminated").IsNull());
		TEST_PASSED();
	}
} // namespace Testing
