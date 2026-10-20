#include "DapExceptionTest.h"
#include "DapTestSupport.h"

using namespace Testing::Dap;

namespace Testing
{
	namespace
	{
		const char* const kThrows =
			"class Oops { public int32 code; }\n"
			"void Fail(int32 code) { Oops o; o.code = code; throw o; } // throw-site\n"
			"int32 Guarded() {\n"
			"\ttry { Fail(1); }\n"
			"\tcatch (Oops e) { return e.code; }\n"
			"\treturn 0;\n"
			"}\n"
			"int32 main() {\n"
			"\tint32 caught = Guarded();\n"
			"\tif (caught == 1) Fail(caught + 1); // uncaught-call\n"
			"\treturn 0;\n"
			"}\n";

		// initialize, launch, the exception filters, configurationDone.
		void StartWithFilters(DapClient& client, const DebugProgram& program, std::initializer_list<const char*> filters)
		{
			client.Request("initialize");
			client.Request("launch", Object({ { "program", Str(program.exe.string()) } }));
			Json::Value list = Json::Value::MakeArray();
			for (const char* filter : filters) list.Append(Str(filter));
			client.Request("setExceptionBreakpoints", Object({ { "filters", list } }));
			client.Request("configurationDone");
		}

		// Under a debugger an uncaught exception ends the program the way Windows ends one: with the exception's code
		// (the program's own terminate handler runs only when no debugger is attached).
		constexpr int64 kUncaughtExitCode = 0xE06D7363;

		std::string Description(const Json::Value& stop) { return stop.Member("body").Member("description").GetString(); }
		std::string ReasonOf(const Json::Value& stop) { return stop.Member("body").Member("reason").GetString(); }

		bool StackHas(DapClient& client, int64 thread, const std::string& function)
		{
			const Json::Value frames = client.Request("stackTrace", Object({ { "threadId", Num(thread) } })).Member("body").Member("stackFrames");
			for (uint32_t i = 0; i < frames.Size(); ++i)
				if (frames.At(i).Member("name").GetString() == function) return true;
			return false;
		}
	}

	void DapExceptionTest::AnUncaughtExceptionStopsTheProgram()
	{
		const DebugProgram program = BuildProgram("uncaught", kThrows);
		TEST_ASSERT(program.built);
		DapClient client;
		StartWithFilters(client, program, { "uncaught" });
		const Json::Value stop = client.WaitEvent("stopped");
		TEST_ASSERT_EQ(ReasonOf(stop), std::string("exception"));
		TEST_ASSERT_EQ(Description(stop), std::string("Oops was thrown and not caught"));
		const int64 thread = stop.Member("body").Member("threadId").GetInt();
		TEST_ASSERT(StackHas(client, thread, "Fail")); // the stack is still the one the throw left
		TEST_ASSERT(StackHas(client, thread, "main"));
		client.Request("continue", Object({ { "threadId", Num(thread) } }));
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), kUncaughtExitCode);
		TEST_PASSED();
	}

	void DapExceptionTest::TheThrownFilterStopsAtEachThrow()
	{
		const DebugProgram program = BuildProgram("thrown", kThrows);
		TEST_ASSERT(program.built);
		DapClient client;
		StartWithFilters(client, program, { "thrown" });
		const Json::Value caught = client.WaitEvent("stopped"); // the throw Guarded catches
		TEST_ASSERT_EQ(ReasonOf(caught), std::string("exception"));
		TEST_ASSERT_EQ(Description(caught), std::string("Oops was thrown"));
		const int64 thread = caught.Member("body").Member("threadId").GetInt();
		TEST_ASSERT(StackHas(client, thread, "Guarded"));
		client.Request("continue", Object({ { "threadId", Num(thread) } }));
		const Json::Value uncaught = client.WaitEvent("stopped"); // the second throw; "uncaught" is off
		TEST_ASSERT_EQ(Description(uncaught), std::string("Oops was thrown"));
		TEST_ASSERT(!StackHas(client, thread, "Guarded"));
		client.Request("continue", Object({ { "threadId", Num(thread) } }));
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), kUncaughtExitCode);
		TEST_ASSERT_EQ(client.History("stopped").size(), size_t(2));
		TEST_PASSED();
	}

	void DapExceptionTest::WithoutFiltersNothingStops()
	{
		const DebugProgram program = BuildProgram("nofilters", kThrows);
		TEST_ASSERT(program.built);
		DapClient client;
		StartWithFilters(client, program, {});
		TEST_ASSERT_EQ(client.WaitEvent("exited").Member("body").Member("exitCode").GetInt(), kUncaughtExitCode);
		TEST_ASSERT(client.History("stopped").empty());
		TEST_PASSED();
	}
} // namespace Testing
