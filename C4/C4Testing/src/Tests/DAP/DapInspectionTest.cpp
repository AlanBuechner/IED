#include "DapInspectionTest.h"
#include "DapTestSupport.h"
#include <map>

using namespace Testing::Dap;

namespace Testing
{
	namespace
	{
		const char* const kValues =
			"enum class Mode : int32 { Off, On }\n"
			"struct Point { public int32 x; public int32 y; }\n"
			"class Shape { public int32 sides; }\n"
			"class Square : public Shape { public float size; }\n"
			"union Bits { struct { public uint16 low; public uint16 high; }; public int32 whole; }\n"
			"namespace Tally { int32 g_count = 4; }\n"
			"int32 Use(Point& p, Point* q, int32[3] values, const char8* name, Mode m, bool flag, double ratio, Bits bits, Square& square,\n"
			"\tPoint* none) {\n"
			"\treturn p.x + q->y + values[2] + (int32)m; // use-body\n"
			"}\n"
			"int32 main() {\n"
			"\tPoint p; p.x = 1; p.y = 2;\n"
			"\tPoint other; other.x = 3; other.y = 4;\n"
			"\tint32[3] values = { 5, 6, 7 };\n"
			"\tBits bits; bits.whole = 131073;\n"
			"\tSquare square; square.sides = 4; square.size = 2.5;\n"
			"\treturn Use(p, &other, values, \"hi\", Mode::On, true, 0.5, bits, square, null) - 13;\n"
			"}\n";

		// A session stopped in Use, and its frame.
		struct StoppedInUse
		{
			DebugProgram program;
			DapClient client;
			int64 frame = 0;
			bool ready = false;

			explicit StoppedInUse(const char* name) : program(BuildProgram(name, kValues))
			{
				if (!program.built) return;
				const Json::Value stop = client.Start(program, { Dap::LineOf(kValues, "use-body") });
				if (stop.IsNull()) return;
				frame = client.TopFrame(stop.Member("body").Member("threadId").GetInt()).Member("id").GetInt();
				ready = true;
			}

			std::map<std::string, Json::Value> Locals()
			{
				std::map<std::string, Json::Value> named;
				for (const auto& [name, variable] : client.Locals(frame)) named[name] = variable;
				return named;
			}

			std::map<std::string, std::string> Children(const Json::Value& variable)
			{
				std::map<std::string, std::string> named;
				const Json::Value list = client.Request("variables", Object({ { "variablesReference", variable.Member("variablesReference") } }))
					.Member("body").Member("variables");
				for (uint32_t i = 0; i < list.Size(); ++i) named[list.At(i).Member("name").GetString()] = list.At(i).Member("value").GetString();
				return named;
			}
		};

		std::string ValueOf(const Json::Value& variable) { return variable.Member("value").GetString(); }
		std::string TypeOf(const Json::Value& variable) { return variable.Member("type").GetString(); }
		bool Opens(const Json::Value& variable) { return variable.Member("variablesReference").GetInt() != 0; }
	}

	void DapInspectionTest::EveryKindOfValueShows()
	{
		StoppedInUse session("values");
		TEST_ASSERT(session.ready);
		std::map<std::string, Json::Value> locals = session.Locals();
		TEST_ASSERT_EQ(ValueOf(locals["p"]), std::string("{x=1, y=2}"));
		TEST_ASSERT_EQ(TypeOf(locals["p"]), std::string("Point&"));
		TEST_ASSERT(ValueOf(locals["q"]).rfind("0x", 0) == 0);
		TEST_ASSERT_EQ(TypeOf(locals["q"]), std::string("Point*"));
		TEST_ASSERT_EQ(ValueOf(locals["values"]), std::string("{5, 6, 7}"));
		TEST_ASSERT_EQ(TypeOf(locals["values"]), std::string("int32[3]"));
		TEST_ASSERT(ValueOf(locals["name"]).find("\"hi\"") != std::string::npos);
		TEST_ASSERT_EQ(TypeOf(locals["name"]), std::string("char8*"));
		TEST_ASSERT(!Opens(locals["name"])); // its text is its value
		TEST_ASSERT_EQ(ValueOf(locals["m"]), std::string("On"));
		TEST_ASSERT_EQ(TypeOf(locals["m"]), std::string("Mode"));
		TEST_ASSERT_EQ(ValueOf(locals["flag"]), std::string("true"));
		TEST_ASSERT_EQ(ValueOf(locals["ratio"]), std::string("0.5"));
		TEST_ASSERT_EQ(TypeOf(locals["ratio"]), std::string("double"));
		TEST_ASSERT_EQ(ValueOf(locals["bits"]), std::string("{low=1, high=2, whole=131073}"));
		TEST_ASSERT_EQ(ValueOf(locals["square"]), std::string("{size=2.5}"));
		TEST_ASSERT_EQ(ValueOf(locals["none"]), std::string("null"));
		TEST_ASSERT(!Opens(locals["none"]));
		TEST_PASSED();
	}

	void DapInspectionTest::ChildrenOpenObjectsPointersAndArrays()
	{
		StoppedInUse session("children");
		TEST_ASSERT(session.ready);
		std::map<std::string, Json::Value> locals = session.Locals();
		std::map<std::string, std::string> pointed = session.Children(locals["q"]); // a pointer opens onto its object
		TEST_ASSERT_EQ(pointed["x"], std::string("3"));
		TEST_ASSERT_EQ(pointed["y"], std::string("4"));
		std::map<std::string, std::string> elements = session.Children(locals["values"]);
		TEST_ASSERT_EQ(elements.size(), size_t(3));
		TEST_ASSERT_EQ(elements["[2]"], std::string("7"));
		std::map<std::string, std::string> square = session.Children(locals["square"]); // a reference: its object
		TEST_ASSERT_EQ(square["size"], std::string("2.5"));
		TEST_ASSERT_EQ(square["Shape"], std::string("{sides=4}")); // the base comes first, under its name
		std::map<std::string, std::string> bits = session.Children(locals["bits"]);
		TEST_ASSERT_EQ(bits["high"], std::string("2"));
		TEST_PASSED();
	}

	void DapInspectionTest::EvaluateFollowsPaths()
	{
		StoppedInUse session("evaluate");
		TEST_ASSERT(session.ready);
		DapClient& client = session.client;
		TEST_ASSERT_EQ(client.Evaluate("p.x", session.frame), std::string("1"));
		TEST_ASSERT_EQ(client.Evaluate("q->y", session.frame), std::string("4"));
		TEST_ASSERT_EQ(client.Evaluate("*q", session.frame), std::string("{x=3, y=4}"));
		TEST_ASSERT_EQ(client.Evaluate("values[2]", session.frame), std::string("7"));
		TEST_ASSERT_EQ(client.Evaluate("square.sides", session.frame), std::string("4")); // through the base
		TEST_ASSERT_EQ(client.Evaluate("bits.high", session.frame), std::string("2"));
		TEST_ASSERT_EQ(client.Evaluate("Tally::g_count", session.frame), std::string("4"));
		TEST_ASSERT_EQ(client.Evaluate("m", session.frame), std::string("On"));
		TEST_PASSED();
	}

	void DapInspectionTest::EvaluateExplainsWhatItCannotRead()
	{
		StoppedInUse session("evaluateerrors");
		TEST_ASSERT(session.ready);
		DapClient& client = session.client;
		TEST_ASSERT(client.Evaluate("nothing", session.frame).find("no variable named nothing") != std::string::npos);
		TEST_ASSERT(client.Evaluate("p.z", session.frame).find("p.z is not a value") != std::string::npos);
		TEST_ASSERT(client.Evaluate("values[3]", session.frame).find("values[3] is not a value") != std::string::npos);
		TEST_ASSERT(client.Evaluate("*none", session.frame).find("does not point at a value") != std::string::npos);
		TEST_ASSERT(client.Evaluate("p +", session.frame).rfind("error: ", 0) == 0);
		TEST_PASSED();
	}
} // namespace Testing
