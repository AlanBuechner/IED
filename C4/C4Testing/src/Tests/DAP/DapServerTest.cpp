#include "DapServerTest.h"
#include "DapTestSupport.h"

using namespace Testing::Dap;

namespace Testing
{
	void DapServerTest::InitializeAnswersThenSaysInitialized()
	{
		DapClient client;
		const Json::Value response = client.Request("initialize", Object({ { "adapterID", Str("c4") } }));
		TEST_ASSERT(response.Member("success").GetBool());
		const Json::Value body = response.Member("body");
		TEST_ASSERT(body.Member("supportsConfigurationDoneRequest").GetBool());
		TEST_ASSERT(body.Member("supportsEvaluateForHovers").GetBool());
		TEST_ASSERT(!body.Member("supportsSetVariable").GetBool(true));
		const Json::Value filters = body.Member("exceptionBreakpointFilters");
		TEST_ASSERT_EQ(filters.Size(), 2u);
		TEST_ASSERT_EQ(filters.At(0).Member("filter").GetString(), std::string("thrown"));
		TEST_ASSERT(!filters.At(0).Member("default").GetBool(true));
		TEST_ASSERT_EQ(filters.At(1).Member("filter").GetString(), std::string("uncaught"));
		TEST_ASSERT(filters.At(1).Member("default").GetBool());
		const Json::Value initialized = client.WaitEvent("initialized");
		TEST_ASSERT(!initialized.IsNull());
		TEST_ASSERT_GT(initialized.Member("seq").GetInt(), response.Member("seq").GetInt());
		TEST_PASSED();
	}

	void DapServerTest::AnUnknownRequestFails()
	{
		DapClient client;
		const Json::Value response = client.Request("restart");
		TEST_ASSERT(!response.IsNull());
		TEST_ASSERT(!response.Member("success").GetBool(true));
		TEST_ASSERT(response.Member("message").GetString().find("restart") != std::string::npos);
		TEST_ASSERT_EQ(response.Member("command").GetString(), std::string("restart"));
		TEST_PASSED();
	}

	void DapServerTest::LaunchNeedsAProgramThatExists()
	{
		DapClient client;
		client.Request("initialize");
		const Json::Value none = client.Request("launch");
		TEST_ASSERT(!none.Member("success").GetBool(true));
		TEST_ASSERT(none.Member("message").GetString().find("program") != std::string::npos);
		const Json::Value missing = client.Request("launch", Object({ { "program", Str("C:\\no\\such\\program.exe") } }));
		TEST_ASSERT(!missing.Member("success").GetBool(true));
		TEST_ASSERT(missing.Member("message").GetString().find("no program at") != std::string::npos);
		TEST_PASSED();
	}

	void DapServerTest::BreakpointsBeforeLaunchWaitForTheProgram()
	{
		DapClient client;
		const Json::Value response = client.Request("setBreakpoints",
			Object({ { "source", Object({ { "path", Str("C:\\work\\main.cf") } }) }, { "breakpoints", [] {
				Json::Value list = Json::Value::MakeArray();
				list.Append(Object({ { "line", Num(4) } }));
				list.Append(Object({ { "line", Num(9) } }));
				return list;
			}() } }));
		const Json::Value breakpoints = response.Member("body").Member("breakpoints");
		TEST_ASSERT_EQ(breakpoints.Size(), 2u);
		TEST_ASSERT(!breakpoints.At(0).Member("verified").GetBool(true));
		TEST_ASSERT_EQ(breakpoints.At(0).Member("line").GetInt(), int64(4));
		TEST_ASSERT(breakpoints.At(0).Member("message").GetString().find("not loaded") != std::string::npos);
		TEST_ASSERT_NE(breakpoints.At(0).Member("id").GetInt(), breakpoints.At(1).Member("id").GetInt());
		TEST_PASSED();
	}

	void DapServerTest::SetBreakpointsNeedsASource()
	{
		DapClient client;
		const Json::Value response = client.Request("setBreakpoints", Object({ { "breakpoints", Json::Value::MakeArray() } }));
		TEST_ASSERT(!response.Member("success").GetBool(true));
		TEST_PASSED();
	}

	void DapServerTest::InspectingWithoutAStopFindsNothing()
	{
		DapClient client;
		const Json::Value evaluate = client.Request("evaluate", Object({ { "expression", Str("total") } }));
		TEST_ASSERT(!evaluate.Member("success").GetBool(true));
		TEST_ASSERT(evaluate.Member("message").GetString().find("not stopped") != std::string::npos);
		TEST_ASSERT_EQ(client.Request("stackTrace", Object({ { "threadId", Num(1) } })).Member("body").Member("stackFrames").Size(), 0u);
		TEST_ASSERT_EQ(client.Request("variables", Object({ { "variablesReference", Num(5) } })).Member("body").Member("variables").Size(), 0u);
		TEST_ASSERT_EQ(client.Request("scopes", Object({ { "frameId", Num(1) } })).Member("body").Member("scopes").Size(), 0u);
		TEST_ASSERT_EQ(client.Request("threads").Member("body").Member("threads").Size(), 0u);
		TEST_PASSED();
	}
} // namespace Testing
