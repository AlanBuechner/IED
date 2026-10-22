#include "DapOptimizedTest.h"
#include "DapTestSupport.h"
#include "Target/Frames.h"
#include "Target/Symbols.h"
#include <dbghelp.h>
#include <cstring>
#include <map>

using namespace Testing::Dap;

namespace Testing
{
	namespace
	{
		constexpr int kOptimized = 3;
		// DbgHelp's SYMFLAG_NULL: no location at the scope's address.
		constexpr uint32_t kNoLocationHere = 0x00080000;
		// CodeView register numbers (CV_AMD64_*).
		constexpr uint32_t kAl = 1, kAh = 5, kAx = 9, kEax = 17, kSi = 15, kEsi = 23, kRip = 33;
		constexpr uint32_t kSil = 324, kRax = 328, kRsp = 335, kR9 = 337, kR9b = 345, kR9w = 353, kR9d = 361;
		constexpr uint32_t kXmm0 = 154, kXmm0Lane0 = 162, kXmm9 = 253;

		// A loop the optimizer cannot count ahead (its bound comes from the clock) with a call in it, so its counters
		// live in registers across the call.
		const char* const kLoop =
			"int32 main() {\n"
			"\tconst int64 count = (clock_now_ns() % 2) + 4;\n"
			"\tint64 total = 0;\n"
			"\tfor (int64 i = 0; i < count; ++i) {\n"
			"\t\ttotal += i * 3;\n"
			"\t\tprintf(\"%lld\\n\", total); // loop-print\n"
			"\t}\n"
			"\treturn total > 0 ? 0 : 1;\n"
			"}\n";

		// Report inlined into Sum and Sum into main (twice): the optimizer leaves no call of either.
		const char* const kInlined =
			"static int64 Report(int64 value, int64 by) {\n"
			"\tconst int64 scaled = value * by;\n"
			"\tprintf(\"scaled %lld\\n\", scaled); // report-print\n"
			"\treturn scaled;\n"
			"}\n"
			"\n"
			"static int64 Sum(int64 count) {\n"
			"\tint64 total = 0;\n"
			"\tfor (int64 i = 0; i < count; ++i) total += Report(i, 3); // sum-first\n"
			"\tfor (int64 j = 0; j < count; ++j) total += Report(j, 5);\n"
			"\tprintf(\"total %lld\\n\", total);\n"
			"\treturn total;\n"
			"}\n"
			"\n"
			"int32 main() {\n"
			"\tconst int64 count = (clock_now_ns() % 2) + 4;\n"
			"\tconst int64 total = Sum(count) + Sum(count + 1); // main-call\n"
			"\tprintf(\"done %lld\\n\", total); // main-print\n"
			"\treturn total > 0 ? 0 : 1;\n"
			"}\n";

		int64 ThreadOf(const Json::Value& stop) { return stop.Member("body").Member("threadId").GetInt(); }

		Json::Value FramesOf(DapClient& client, int64 thread)
		{
			return client.Request("stackTrace", Object({ { "threadId", Num(thread) } })).Member("body").Member("stackFrames");
		}

		std::string NameOf(const Json::Value& frame) { return frame.Member("name").GetString(); }
		int64 LineAt(const Json::Value& frame) { return frame.Member("line").GetInt(); }
		std::string PcOf(const Json::Value& frame) { return frame.Member("instructionPointerReference").GetString(); }

		std::map<std::string, std::string> LocalTexts(DapClient& client, int64 frameId)
		{
			std::map<std::string, std::string> texts;
			for (const auto& [name, variable] : client.Locals(frameId)) texts[name] = variable.Member("value").GetString();
			return texts;
		}
	}

	void DapOptimizedTest::StorageFollowsDbgHelpsFlags()
	{
		using C4DAP::Target::StorageOf;
		using Kind = C4DAP::Target::SymbolStorage::Kind;
		TEST_ASSERT(StorageOf(SYMFLAG_LOCAL | kNoLocationHere, 0, 0, 0).kind == Kind::OptimizedAway);
		TEST_ASSERT(StorageOf(SYMFLAG_PARAMETER | SYMFLAG_REGISTER | kNoLocationHere, kRax, 0, 0).kind == Kind::OptimizedAway);
		const C4DAP::Target::SymbolStorage constant = StorageOf(SYMFLAG_LOCAL | SYMFLAG_CONSTANT, 0, 0, 42);
		TEST_ASSERT(constant.kind == Kind::Constant);
		TEST_ASSERT_EQ(constant.constant, uint64_t(42));
		TEST_ASSERT(StorageOf(SYMFLAG_LOCAL | SYMFLAG_VALUEPRESENT, 0, 0, 7).kind == Kind::Constant);
		const C4DAP::Target::SymbolStorage held = StorageOf(SYMFLAG_LOCAL | SYMFLAG_REGISTER, kRax, 0, 0);
		TEST_ASSERT(held.kind == Kind::Register);
		TEST_ASSERT_EQ(held.reg, kRax);
		const C4DAP::Target::SymbolStorage relative = StorageOf(SYMFLAG_LOCAL | SYMFLAG_REGREL, kRsp, static_cast<uint64_t>(-32), 0);
		TEST_ASSERT(relative.kind == Kind::RegisterRelative);
		TEST_ASSERT_EQ(relative.offset, int64_t(-32));
		TEST_ASSERT(StorageOf(SYMFLAG_LOCAL | SYMFLAG_FRAMEREL, 0, 16, 0).kind == Kind::FrameRelative);
		TEST_ASSERT(StorageOf(0, 0, 0x1000, 0).kind == Kind::Address);
		TEST_PASSED();
	}

	void DapOptimizedTest::RegistersReadTheirParts()
	{
		using C4DAP::Target::RegisterValue;
		CONTEXT c{};
		c.Rip = 0x140001000ull;
		c.Rax = 0x1122334455667788ull;
		c.Rsi = 0x0102030405060708ull;
		c.R9 = 0xA1A2A3A4A5A6A7A8ull;
		const uint64_t xmm0Low = 0x42ull, xmm9Low = 0x3FF0000000000000ull;
		std::memcpy(&c.Xmm0, &xmm0Low, sizeof(xmm0Low));
		std::memcpy(&c.Xmm9, &xmm9Low, sizeof(xmm9Low));
		TEST_ASSERT_EQ(*RegisterValue(c, kRip), uint64_t(0x140001000ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kRax), uint64_t(0x1122334455667788ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kEax), uint64_t(0x55667788ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kAx), uint64_t(0x7788ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kAl), uint64_t(0x88ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kAh), uint64_t(0x77ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kEsi), uint64_t(0x05060708ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kSi), uint64_t(0x0708ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kSil), uint64_t(0x08ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kR9), uint64_t(0xA1A2A3A4A5A6A7A8ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kR9d), uint64_t(0xA5A6A7A8ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kR9w), uint64_t(0xA7A8ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kR9b), uint64_t(0xA8ull));
		TEST_ASSERT_EQ(*RegisterValue(c, kXmm0), xmm0Low);
		TEST_ASSERT_EQ(*RegisterValue(c, kXmm9), xmm9Low);
		TEST_ASSERT(!RegisterValue(c, kXmm0Lane0).has_value());
		TEST_PASSED();
	}

	void DapOptimizedTest::ConstantsAndOptimizedAwayHaveNoAddress()
	{
		using namespace C4DAP::Target;
		const FrameRegisters frame;
		const VariableSymbol factor{ "factor", TypeRef{}, StorageOf(SYMFLAG_LOCAL | SYMFLAG_CONSTANT, 0, 0, 3), false };
		const std::optional<ValueRef> value = VariableValue(factor, frame);
		TEST_ASSERT(value.has_value());
		TEST_ASSERT(value->inRegister);
		TEST_ASSERT_EQ(value->registerBits, uint64_t(3));
		const VariableSymbol start{ "start", TypeRef{}, StorageOf(SYMFLAG_LOCAL | kNoLocationHere, 0, 0, 0), false };
		TEST_ASSERT(!VariableValue(start, frame).has_value());
		TEST_PASSED();
	}

	void DapOptimizedTest::AnOptimizedLoopShowsItsLocals()
	{
		const DebugProgram program = BuildProgram("optimized_loop", kLoop, kOptimized);
		TEST_ASSERT(program.built);
		DapClient client;
		Json::Value stop = client.Start(program, { LineOf(kLoop, "loop-print") });
		TEST_ASSERT(!stop.IsNull());
		for (int hit = 1; hit < 3; ++hit) // the third time round: i = 2, total = 0 + 3 + 6
		{
			client.Request("continue", Object({ { "threadId", Num(ThreadOf(stop)) } }));
			stop = client.WaitEvent("stopped");
			TEST_ASSERT(!stop.IsNull());
		}
		const int64 frameId = client.TopFrame(ThreadOf(stop)).Member("id").GetInt();
		const std::map<std::string, std::string> locals = LocalTexts(client, frameId);
		TEST_ASSERT(locals.count("total") == 1);
		TEST_ASSERT_EQ(locals.at("total"), std::string("9"));
		TEST_ASSERT_EQ(client.Evaluate("total", frameId), std::string("9"));
		TEST_ASSERT(locals.count("i") == 1);
		TEST_ASSERT(locals.at("i") == "2" || locals.at("i") == "<optimized away>");
		if (locals.at("i") != "2") TEST_ASSERT_EQ(client.Evaluate("i", frameId), std::string("error: i is optimized away here"));
		for (const auto& [name, text] : locals) TEST_ASSERT(text != "<unreadable>");
		client.Request("disconnect", Object({ { "terminateDebuggee", Json::Value::Bool(true) } }));
		TEST_PASSED();
	}

	// BUG-352: a breakpoint in an inlined function stops; each inlined call is a frame with its line and locals.
	void DapOptimizedTest::AnInlinedCallIsAFrameOfItsOwn()
	{
		const DebugProgram program = BuildProgram("optimized_inlined", kInlined, kOptimized);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { LineOf(kInlined, "report-print") });
		TEST_ASSERT(!stop.IsNull());
		const int64 thread = ThreadOf(stop);
		const Json::Value frames = FramesOf(client, thread);
		TEST_ASSERT(frames.Size() >= 3);
		TEST_ASSERT_EQ(NameOf(frames.At(0)), std::string("Report"));
		TEST_ASSERT_EQ(LineAt(frames.At(0)), int64(LineOf(kInlined, "report-print")));
		TEST_ASSERT_EQ(NameOf(frames.At(1)), std::string("Sum"));
		TEST_ASSERT_EQ(LineAt(frames.At(1)), int64(LineOf(kInlined, "sum-first")));
		TEST_ASSERT_EQ(NameOf(frames.At(2)), std::string("main"));
		TEST_ASSERT_EQ(LineAt(frames.At(2)), int64(LineOf(kInlined, "main-call")));
		TEST_ASSERT_EQ(PcOf(frames.At(0)), PcOf(frames.At(1))); // inlined: one instruction, three frames
		TEST_ASSERT_EQ(PcOf(frames.At(1)), PcOf(frames.At(2)));
		const std::map<std::string, std::string> report = LocalTexts(client, frames.At(0).Member("id").GetInt());
		TEST_ASSERT(report.count("value") == 1 && report.count("by") == 1 && report.count("scaled") == 1);
		TEST_ASSERT(LocalTexts(client, frames.At(2).Member("id").GetInt()).count("count") == 1);
		client.Request("stepOut", Object({ { "threadId", Num(thread) } }));
		TEST_ASSERT(!client.WaitEvent("stopped").IsNull());
		const Json::Value out = FramesOf(client, thread).At(0);
		TEST_ASSERT_EQ(NameOf(out), std::string("Sum"));
		TEST_ASSERT_EQ(LineAt(out), int64(LineOf(kInlined, "sum-first")));
		client.Request("disconnect", Object({ { "terminateDebuggee", Json::Value::Bool(true) } }));
		TEST_PASSED();
	}

	// BUG-352: the line of a call that was inlined stops in its own frame, once each time it runs, and steps over it.
	void DapOptimizedTest::ABreakpointOnAnInlinedCallsLineStopsThere()
	{
		const DebugProgram program = BuildProgram("optimized_call_line", kInlined, kOptimized);
		TEST_ASSERT(program.built);
		DapClient client;
		const Json::Value stop = client.Start(program, { LineOf(kInlined, "main-call") });
		TEST_ASSERT(!stop.IsNull());
		const int64 thread = ThreadOf(stop);
		const Json::Value top = FramesOf(client, thread).At(0);
		TEST_ASSERT_EQ(NameOf(top), std::string("main"));
		TEST_ASSERT_EQ(LineAt(top), int64(LineOf(kInlined, "main-call")));
		client.Request("next", Object({ { "threadId", Num(thread) } }));
		TEST_ASSERT(!client.WaitEvent("stopped").IsNull());
		TEST_ASSERT_EQ(LineAt(FramesOf(client, thread).At(0)), int64(LineOf(kInlined, "main-print")));
		client.Request("continue", Object({ { "threadId", Num(thread) } }));
		TEST_ASSERT(!client.WaitEvent("exited").IsNull());
		TEST_ASSERT_EQ(client.History("stopped").size(), size_t(2));
		TEST_PASSED();
	}
} // namespace Testing
