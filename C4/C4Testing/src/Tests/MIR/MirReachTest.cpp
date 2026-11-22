#include "MirReachTest.h"
#include "Backend/MIR/MirBuilder.h"
#include "Backend/MIR/MirReach.h"
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		// Owns the hand-built nodes of one test.
		struct Nodes
		{
			std::vector<std::unique_ptr<HIRNode>> owned;

			template <typename T, typename... Args> T* Make(Args&&... args)
			{
				owned.push_back(std::make_unique<T>(std::forward<Args>(args)...));
				return static_cast<T*>(owned.back().get());
			}
			BlockNode* Block(std::vector<HIRNode*> statements)
			{
				BlockNode* block = Make<BlockNode>();
				block->stmts = std::move(statements);
				return block;
			}
			HIRNode* Return() { return Make<ReturnNode>(nullptr); }
			HIRNode* Plain() { return Make<BlockNode>(); } // a statement that just goes on
			const HIRNode* True() { return Make<ConstantNode>(nullptr, ConstantValue::Int(1)); }
			const HIRNode* Unknown() { return Make<ConstantNode>(nullptr, ConstantValue()); } // a constant that is not true
		};

		MirBody Build(BlockNode* root)
		{
			HIRFunction fn;
			fn.root = root;
			return MirBuilder(fn, nullptr).Build();
		}

		// Whether control can run off the end of a body that is `root`.
		bool Completes(BlockNode* root)
		{
			return MirReach::FallsOffEnd(Build(root));
		}

		SourceLocation At(uint32 line)
		{
			SourceLocation at;
			at.line = line;
			return at;
		}
	}

	void MirReachTest::JumpsEndABlock()
	{
		Nodes n;
		TEST_ASSERT(Completes(n.Block({})));
		TEST_ASSERT(Completes(n.Block({ n.Plain(), n.Plain() })));
		TEST_ASSERT(!Completes(n.Block({ n.Plain(), n.Return() })));
		TEST_ASSERT(!Completes(n.Block({ n.Make<ThrowNode>(nullptr, true) })));
		TEST_ASSERT(!Completes(n.Block({ n.Make<GotoNode>(7) })));
		TEST_ASSERT(!Completes(n.Block({ n.Make<JumpNode>(7) })));
		TEST_ASSERT(!Completes(n.Block({ n.Block({ n.Return() }) })));
	}

	void MirReachTest::BranchesCompleteWhenEitherDoes()
	{
		Nodes n;
		auto* both = n.Make<IfNode>(n.Unknown());
		both->thenBlock = n.Block({ n.Return() });
		both->elseBlock = n.Block({ n.Return() });
		TEST_ASSERT(!Completes(n.Block({ both })));
		auto* thenOnly = n.Make<IfNode>(n.Unknown());
		thenOnly->thenBlock = n.Block({ n.Return() });
		TEST_ASSERT(Completes(n.Block({ thenOnly })));
		auto* oneSide = n.Make<IfNode>(n.Unknown());
		oneSide->thenBlock = n.Block({ n.Return() });
		oneSide->elseBlock = n.Block({ n.Plain() });
		TEST_ASSERT(Completes(n.Block({ oneSide })));
		// `if (true)` is not folded: its other way counts, as C++'s rule for a missing return has it.
		auto* always = n.Make<IfNode>(n.True());
		always->thenBlock = n.Block({ n.Return() });
		TEST_ASSERT(Completes(n.Block({ always })));
	}

	void MirReachTest::LoopsEndThroughTheirConditionOrABreak()
	{
		Nodes n;
		auto* endless = n.Make<WhileNode>(n.True());
		endless->breakLabel = 1;
		endless->body = n.Block({ n.Plain() });
		TEST_ASSERT(!Completes(n.Block({ endless })));
		auto* broken = n.Make<WhileNode>(n.True());
		broken->breakLabel = 2;
		broken->body = n.Block({ n.Make<BreakNode>(2) });
		TEST_ASSERT(Completes(n.Block({ broken })));
		auto* conditional = n.Make<WhileNode>(n.Unknown());
		conditional->body = n.Block({ n.Return() });
		TEST_ASSERT(Completes(n.Block({ conditional })));
		auto* doReturn = n.Make<WhileNode>(n.Unknown());
		doReturn->isDoWhile = true;
		doReturn->body = n.Block({ n.Return() });
		TEST_ASSERT(!Completes(n.Block({ doReturn })));       // its condition is never reached
		auto* doContinue = n.Make<WhileNode>(n.Unknown());
		doContinue->isDoWhile = true;
		doContinue->continueLabel = 5;
		doContinue->body = n.Block({ n.Make<ContinueNode>(5) });
		TEST_ASSERT(Completes(n.Block({ doContinue })));
		auto* forever = n.Make<ForNode>();
		forever->breakLabel = 3;
		forever->body = n.Block({ n.Plain() });
		TEST_ASSERT(!Completes(n.Block({ forever })));         // for (;;)
		// A break out of an inner loop does not end the outer one.
		auto* inner = n.Make<WhileNode>(n.True());
		inner->breakLabel = 9;
		inner->body = n.Block({ n.Make<BreakNode>(9) });
		auto* outer = n.Make<WhileNode>(n.True());
		outer->breakLabel = 8;
		outer->body = n.Block({ inner });
		TEST_ASSERT(!Completes(n.Block({ outer })));
	}

	void MirReachTest::SwitchesAndTries()
	{
		Nodes n;
		auto* allReturn = n.Make<SwitchNode>(n.Unknown());
		allReturn->cases.push_back({ ConstantValue::Int(1), n.Block({ n.Return() }), 0 });
		allReturn->defaultBlock = n.Block({ n.Return() });
		TEST_ASSERT(!Completes(n.Block({ allReturn })));
		auto* noDefault = n.Make<SwitchNode>(n.Unknown());
		noDefault->cases.push_back({ ConstantValue::Int(1), n.Block({ n.Return() }), 0 });
		TEST_ASSERT(Completes(n.Block({ noDefault })));       // no case may match
		auto* breaks = n.Make<SwitchNode>(n.Unknown());
		breaks->breakLabel = 4;
		breaks->cases.push_back({ ConstantValue::Int(1), n.Block({ n.Make<BreakNode>(4) }), 0 });
		breaks->defaultBlock = n.Block({ n.Return() });
		TEST_ASSERT(Completes(n.Block({ breaks })));
		auto* attempt = n.Make<TryNode>();
		attempt->tryBlock = n.Block({ n.Return() });
		attempt->catches.push_back({ nullptr, 0, n.Block({ n.Plain() }) });
		TEST_ASSERT(Completes(n.Block({ attempt })));         // the catch can complete
		auto* rethrows = n.Make<TryNode>();
		rethrows->tryBlock = n.Block({ n.Return() });
		rethrows->catches.push_back({ nullptr, 0, n.Block({ n.Make<ThrowNode>(nullptr, true) }) });
		TEST_ASSERT(!Completes(n.Block({ rethrows })));
		// An empty try may raise nothing, yet its handlers count as reached, as C++'s rule has it.
		auto* empty = n.Make<TryNode>();
		empty->tryBlock = n.Block({});
		empty->catches.push_back({ nullptr, 0, n.Block({ n.Return() }) });
		TEST_ASSERT(Completes(n.Block({ empty })));
	}

	void MirReachTest::ALabelRevivesTheCodeAfterIt()
	{
		Nodes n;
		TEST_ASSERT(Completes(n.Block({ n.Make<GotoNode>(3), n.Make<LabelNode>(3), n.Plain() })));
		TEST_ASSERT(!Completes(n.Block({ n.Make<LabelNode>(3), n.Return() })));
		// A label no goto names still counts as reached (a goto may name it).
		TEST_ASSERT(Completes(n.Block({ n.Return(), n.Make<LabelNode>(4), n.Plain() })));
	}

	void MirReachTest::ACaseThatCanEndRunsIntoTheNext()
	{
		Nodes n;
		auto* falls = n.Make<SwitchNode>(n.Unknown());
		falls->breakLabel = 6;
		SwitchCase first{ ConstantValue::Int(1), n.Block({ n.Plain() }), 0 };
		first.fallCheck = At(12);
		falls->cases.push_back(first);
		falls->cases.push_back({ ConstantValue::Int(2), n.Block({ n.Make<BreakNode>(6) }), 0 });
		const QueryResult<void> checked = MirReach::CheckCases(Build(n.Block({ falls })));
		TEST_ASSERT(!checked.Ok());
		TEST_ASSERT(checked.loc.line == 12);
		TEST_ASSERT(std::string(checked.message.c_str()).find("this case falls into the next one") != std::string::npos);

		auto* ends = n.Make<SwitchNode>(n.Unknown());
		ends->breakLabel = 7;
		SwitchCase broken{ ConstantValue::Int(1), n.Block({ n.Plain(), n.Make<BreakNode>(7) }), 0 };
		broken.fallCheck = At(20);
		ends->cases.push_back(broken);
		ends->cases.push_back({ ConstantValue::Int(2), n.Block({ n.Return() }), 0 });
		TEST_ASSERT(MirReach::CheckCases(Build(n.Block({ ends }))).Ok());

		// The case's own start is what counts: a switch after a return is still checked (the check is the case's own).
		auto* dead = n.Make<SwitchNode>(n.Unknown());
		SwitchCase deadCase{ ConstantValue::Int(1), n.Block({ n.Plain() }), 0 };
		deadCase.fallCheck = At(30);
		dead->cases.push_back(deadCase);
		dead->cases.push_back({ ConstantValue::Int(2), n.Block({ n.Return() }), 0 });
		TEST_ASSERT(!MirReach::CheckCases(Build(n.Block({ n.Return(), dead }))).Ok());
	}

	void MirReachTest::ANestedTryCatchesWhatTheInnerOneLetsGo()
	{
		Nodes n;
		auto* inner = n.Make<TryNode>();
		inner->tryBlock = n.Block({ n.Return() });
		TryNode::CatchClause typed;
		typed.type = nullptr;
		typed.block = n.Block({ n.Return() });
		typed.catchAll = false;
		inner->catches.push_back(typed);
		auto* outer = n.Make<TryNode>();
		outer->tryBlock = n.Block({ inner, n.Return() });
		TryNode::CatchClause all;
		all.block = n.Block({ n.Plain() });
		all.catchAll = true;
		outer->catches.push_back(all);
		TEST_ASSERT(Completes(n.Block({ outer }))); // what the inner clause does not take reaches the outer handler

		auto* takesAll = n.Make<TryNode>();
		takesAll->tryBlock = n.Block({ n.Return() });
		TryNode::CatchClause everything;
		everything.block = n.Block({ n.Return() });
		everything.catchAll = true;
		takesAll->catches.push_back(everything);
		const MirBody mir = Build(n.Block({ takesAll }));
		TEST_ASSERT(!MirReach::FallsOffEnd(mir));
	}
} // namespace Testing
