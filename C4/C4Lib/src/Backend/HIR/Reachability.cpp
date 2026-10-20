#include "Backend/HIR/Reachability.h"
#include <set>

namespace Scripting::Reachability
{
	namespace
	{
		// What a statement does to the flow: whether control reaches its end, the loops and switches a break
		// inside it leaves, and the loops a continue inside it goes on with (by label).
		struct Flow
		{
			bool completes = true;
			std::set<uint32> breaks;
			std::set<uint32> continues;

			void Absorb(const Flow& other)
			{
				breaks.insert(other.breaks.begin(), other.breaks.end());
				continues.insert(other.continues.begin(), other.continues.end());
			}
		};

		Flow Statement(const HIRNode* node);

		Flow Block(const BlockNode* block)
		{
			Flow flow;
			if (!block) return flow;
			for (const HIRNode* statement : block->stmts)
			{
				if (statement->kind == HIRNode::Kind::Label) flow.completes = true;
				if (!flow.completes) continue;
				const Flow inner = Statement(statement);
				flow.Absorb(inner);
				flow.completes = inner.completes;
			}
			return flow;
		}

		bool AlwaysTrue(const HIRNode* condition)
		{
			if (!condition) return true; // `for (;;)`
			const auto* constant = condition->kind == HIRNode::Kind::Constant ? static_cast<const ConstantNode*>(condition) : nullptr;
			return constant && constant->value.AsInt() != 0;
		}

		// A loop ends through its condition, or through a break that targets it. A do-while reaches its
		// condition only when its body completes or continues.
		Flow Loop(const BlockNode* body, const HIRNode* condition, uint32 breakLabel, uint32 continueLabel, bool conditionFirst)
		{
			Flow flow = Block(body);
			const bool reachesCondition = conditionFirst || flow.completes || flow.continues.count(continueLabel) != 0;
			flow.completes = (reachesCondition && !AlwaysTrue(condition)) || flow.breaks.count(breakLabel) != 0;
			flow.breaks.erase(breakLabel);
			flow.continues.erase(continueLabel);
			return flow;
		}

		// A switch ends when a case runs off its end, a break leaves it, or no case matches (no default).
		Flow Switch(const SwitchNode* node)
		{
			Flow flow;
			bool someCaseCompletes = false;
			const auto visit = [&](const BlockNode* block) {
				const Flow inner = Block(block);
				flow.Absorb(inner);
				someCaseCompletes = someCaseCompletes || inner.completes;
			};
			for (const SwitchCase& c : node->cases) visit(c.block);
			if (node->defaultBlock) visit(node->defaultBlock);
			flow.completes = !node->defaultBlock || someCaseCompletes || flow.breaks.count(node->breakLabel) != 0;
			flow.breaks.erase(node->breakLabel);
			return flow;
		}

		Flow Try(const TryNode* node)
		{
			Flow flow = Block(node->tryBlock);
			for (const TryNode::CatchClause& clause : node->catches)
			{
				const Flow inner = Block(clause.block);
				flow.Absorb(inner);
				flow.completes = flow.completes || inner.completes;
			}
			return flow;
		}

		Flow Statement(const HIRNode* node)
		{
			switch (node->kind)
			{
			case HIRNode::Kind::Return:
			case HIRNode::Kind::Throw:
			case HIRNode::Kind::Jump:
			case HIRNode::Kind::Goto:
				return { false, {}, {} };
			case HIRNode::Kind::Break:
				return { false, { static_cast<const BreakNode*>(node)->targetLabel }, {} };
			case HIRNode::Kind::Continue:
				return { false, {}, { static_cast<const ContinueNode*>(node)->targetLabel } };
			case HIRNode::Kind::Block:
				return Block(static_cast<const BlockNode*>(node));
			case HIRNode::Kind::If:
			{
				const auto* branch = static_cast<const IfNode*>(node);
				Flow flow = Block(branch->thenBlock);
				const Flow otherwise = branch->elseBlock ? Block(branch->elseBlock) : Flow{};
				flow.Absorb(otherwise);
				flow.completes = flow.completes || otherwise.completes;
				return flow;
			}
			case HIRNode::Kind::While:
			{
				const auto* loop = static_cast<const WhileNode*>(node);
				return Loop(loop->body, loop->cond, loop->breakLabel, loop->continueLabel, !loop->isDoWhile);
			}
			case HIRNode::Kind::For:
			{
				const auto* loop = static_cast<const ForNode*>(node);
				Flow flow = Block(loop->initBlock);
				if (!flow.completes) return flow;
				const Flow body = Loop(loop->body, loop->cond, loop->breakLabel, loop->continueLabel, true);
				flow.Absorb(body);
				flow.completes = body.completes;
				return flow;
			}
			case HIRNode::Kind::Switch:
				return Switch(static_cast<const SwitchNode*>(node));
			case HIRNode::Kind::TryCatch:
				return Try(static_cast<const TryNode*>(node));
			default:
				return {}; // an expression, a declaration, a store, a defer: control goes on
			}
		}
	}

	bool CompletesNormally(const BlockNode& block)
	{
		return Block(&block).completes;
	}
}
