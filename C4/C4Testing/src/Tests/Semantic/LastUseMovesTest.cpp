#include "LastUseMovesTest.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Compiler/ErrorTracker.h"
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		constexpr const char* kRes =
			"struct Res { public int32 id; [operator(\"copy\")] public void Copy(const Res& o) { id = o.id; } "
			"[operator(\"delete\")] public void Delete() {} }\n"
			"int32 Peek(const Res& r) { return r.id; }\n"
			"int32 Sink(Res r) { return r.id; }\n";

		ProgramModel& MakeModel(CompilerInstance& compiler, const std::string& source)
		{
			compiler.RegisterInMemory("t.cf", source.c_str());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetProgramModelForLowering();
		}

		// The transfers of F's body: how many there are and how many move.
		struct Transfers { int total = 0; int moves = 0; int flagged = 0; bool complete = false; };

		Transfers Lower(const std::string& body)
		{
			CompilerInstance compiler;
			auto& model = MakeModel(compiler, std::string(kRes) + body);
			const FunctionRecord* rec = nullptr;
			auto r = model.QueryFunction("F", nullptr, nullptr, {}, {}, &rec);
			Transfers t;
			if (!r.Ok() || !rec) return t;
			ErrorTracker errors;
			HIRFunction fn = LowerFunction(model, errors, rec);
			t.complete = fn.complete && errors.GetErrors().Empty();
			for (const auto& node : fn.m_Nodes)
			{
				if (node->kind != HIRNode::Kind::Transfer) continue;
				const auto* transfer = static_cast<const TransferNode*>(node);
				++t.total;
				t.moves += transfer->move ? 1 : 0;
				t.flagged += transfer->dropFlag != kNoDropFlag ? 1 : 0;
			}
			return t;
		}
	}

	void LastUseMovesTest::LastCopyMovesWithADropFlag()
	{
		const Transfers t = Lower("void F() { Res a; Res b = a; }\n");
		TEST_ASSERT(t.complete && t.total == 1 && t.moves == 1 && t.flagged == 1);
	}

	void LastUseMovesTest::LaterUseKeepsTheCopy()
	{
		const Transfers t = Lower("void F() { Res a; Res b = a; Peek(a); }\n");
		TEST_ASSERT(t.complete && t.total == 1 && t.moves == 0);
		const Transfers both = Lower("void F() { Res a; Res b = a; Res c = a; }\n");
		TEST_ASSERT(both.total == 2 && both.moves == 1); // only the second is the last use
	}

	void LastUseMovesTest::ReferenceArgumentDoesNotEscape()
	{
		const Transfers t = Lower("void F() { Res a; Peek(a); Sink(a); }\n");
		TEST_ASSERT(t.complete && t.total == 1 && t.moves == 1);
	}

	void LastUseMovesTest::TakenAddressEscapes()
	{
		const Transfers address = Lower("void F() { Res a; Res* p = &a; Res b = a; }\n");
		TEST_ASSERT(address.complete && address.total == 1 && address.moves == 0);
		const Transfers reference = Lower("void F() { Res a; Res& r = a; Res b = a; }\n");
		TEST_ASSERT(reference.complete && reference.total == 1 && reference.moves == 0);
	}

	void LastUseMovesTest::LoopKeepsTheCopy()
	{
		const Transfers outside = Lower("void F() { Res a; for (int32 i = 0; i < 2; ++i) { Res b = a; } }\n");
		TEST_ASSERT(outside.complete && outside.total == 1 && outside.moves == 0);
		const Transfers inside = Lower("void F() { for (int32 i = 0; i < 2; ++i) { Res a; Res b = a; } }\n");
		TEST_ASSERT(inside.complete && inside.total == 1 && inside.moves == 1);
	}

	void LastUseMovesTest::DeferUseKeepsTheCopy()
	{
		const Transfers t = Lower("void F() { Res a; defer { Peek(a); } Res b = a; }\n");
		TEST_ASSERT(t.complete && t.total == 1 && t.moves == 0);
	}

	void LastUseMovesTest::GotoKeepsEveryCopy()
	{
		const Transfers t = Lower("void F() { Res a; Res b = a; goto done; done: return; }\n");
		TEST_ASSERT(t.complete && t.total == 1 && t.moves == 0);
	}

	void LastUseMovesTest::AssignedVariableStillMoves()
	{
		// An assignment's own result address does not let the target escape; binding a reference to it does.
		const Transfers t = Lower("void F() { Res a; Res c; c = a; Res b = c; }\n");
		TEST_ASSERT(t.complete && t.total == 2 && t.moves == 2);
		const Transfers bound = Lower("void F() { Res a; Res c; Res& r = (c = a); Res b = c; }\n");
		TEST_ASSERT(bound.complete && bound.total == 2 && bound.moves == 1); // `a` still moves into `c`
	}

	void LastUseMovesTest::TriviallyCopiedTypesHaveNoTransfers()
	{
		const Transfers t = Lower("struct P { public int32 x; }\nvoid F() { P a; P b = a; }\n");
		TEST_ASSERT(t.complete && t.total == 0);
	}
} // namespace Testing
