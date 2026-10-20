#include "ReachabilityTest.h"
#include "Backend/HIR/Reachability.h"
#include <memory>
#include <vector>

namespace Testing
{
	using namespace Scripting;

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
	}

	void ReachabilityTest::JumpsEndABlock()
	{
		Nodes n;
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({})));
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ n.Plain(), n.Plain() })));
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ n.Plain(), n.Return() })));
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ n.Make<ThrowNode>(nullptr, true) })));
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ n.Make<GotoNode>(7) })));
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ n.Make<JumpNode>(7) })));
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ n.Block({ n.Return() }) })));
	}

	void ReachabilityTest::BranchesCompleteWhenEitherDoes()
	{
		Nodes n;
		auto* both = n.Make<IfNode>(n.Unknown());
		both->thenBlock = n.Block({ n.Return() });
		both->elseBlock = n.Block({ n.Return() });
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ both })));
		auto* thenOnly = n.Make<IfNode>(n.Unknown());
		thenOnly->thenBlock = n.Block({ n.Return() });
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ thenOnly })));
		auto* oneSide = n.Make<IfNode>(n.Unknown());
		oneSide->thenBlock = n.Block({ n.Return() });
		oneSide->elseBlock = n.Block({ n.Plain() });
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ oneSide })));
	}

	void ReachabilityTest::LoopsEndThroughTheirConditionOrABreak()
	{
		Nodes n;
		auto* endless = n.Make<WhileNode>(n.True());
		endless->breakLabel = 1;
		endless->body = n.Block({ n.Plain() });
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ endless })));
		auto* broken = n.Make<WhileNode>(n.True());
		broken->breakLabel = 2;
		broken->body = n.Block({ n.Make<BreakNode>(2) });
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ broken })));
		auto* conditional = n.Make<WhileNode>(n.Unknown());
		conditional->body = n.Block({ n.Return() });
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ conditional })));
		auto* doReturn = n.Make<WhileNode>(n.Unknown());
		doReturn->isDoWhile = true;
		doReturn->body = n.Block({ n.Return() });
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ doReturn })));       // its condition is never reached
		auto* doContinue = n.Make<WhileNode>(n.Unknown());
		doContinue->isDoWhile = true;
		doContinue->continueLabel = 5;
		doContinue->body = n.Block({ n.Make<ContinueNode>(5) });
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ doContinue })));
		auto* forever = n.Make<ForNode>();
		forever->breakLabel = 3;
		forever->body = n.Block({ n.Plain() });
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ forever })));         // for (;;)
		// A break out of an inner loop does not end the outer one.
		auto* inner = n.Make<WhileNode>(n.True());
		inner->breakLabel = 9;
		inner->body = n.Block({ n.Make<BreakNode>(9) });
		auto* outer = n.Make<WhileNode>(n.True());
		outer->breakLabel = 8;
		outer->body = n.Block({ inner });
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ outer })));
	}

	void ReachabilityTest::SwitchesAndTries()
	{
		Nodes n;
		auto* allReturn = n.Make<SwitchNode>(n.Unknown());
		allReturn->cases.push_back({ ConstantValue::Int(1), n.Block({ n.Return() }), 0 });
		allReturn->defaultBlock = n.Block({ n.Return() });
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ allReturn })));
		auto* noDefault = n.Make<SwitchNode>(n.Unknown());
		noDefault->cases.push_back({ ConstantValue::Int(1), n.Block({ n.Return() }), 0 });
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ noDefault })));       // no case may match
		auto* breaks = n.Make<SwitchNode>(n.Unknown());
		breaks->breakLabel = 4;
		breaks->cases.push_back({ ConstantValue::Int(1), n.Block({ n.Make<BreakNode>(4) }), 0 });
		breaks->defaultBlock = n.Block({ n.Return() });
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ breaks })));
		auto* attempt = n.Make<TryNode>();
		attempt->tryBlock = n.Block({ n.Return() });
		attempt->catches.push_back({ nullptr, 0, n.Block({ n.Plain() }) });
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ attempt })));         // the catch can complete
		auto* rethrows = n.Make<TryNode>();
		rethrows->tryBlock = n.Block({ n.Return() });
		rethrows->catches.push_back({ nullptr, 0, n.Block({ n.Make<ThrowNode>(nullptr, true) }) });
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ rethrows })));
	}

	void ReachabilityTest::ALabelRevivesTheCodeAfterIt()
	{
		Nodes n;
		TEST_ASSERT(Reachability::CompletesNormally(*n.Block({ n.Make<GotoNode>(3), n.Make<LabelNode>(3), n.Plain() })));
		TEST_ASSERT(!Reachability::CompletesNormally(*n.Block({ n.Make<LabelNode>(3), n.Return() })));
	}
} // namespace Testing
