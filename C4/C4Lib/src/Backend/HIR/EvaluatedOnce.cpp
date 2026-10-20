#include "Backend/HIR/EvaluatedOnce.h"

namespace Scripting
{
	namespace
	{
		bool ReadsNothingThatChanges(const HIRNode* node)
		{
			if (node->kind == HIRNode::Kind::Constant) return true;
			if (node->kind != HIRNode::Kind::Load) return false;
			const LValue* from = static_cast<const LoadNode*>(node)->loc;
			return from->kind == LValue::Kind::Slot || from->kind == LValue::Kind::This;
		}

		// `node` computed once into a temporary; the temporary's value from then on.
		const HIRNode* Held(LoweringContext& ctx, const HIRNode* node, std::vector<const HIRNode*>& steps)
		{
			if (!node || ReadsNothingThatChanges(node)) return node;
			LValue temp; temp.kind = LValue::Kind::Slot; temp.type = node->type; temp.slot = ctx.AllocSlot(node->type, "__once", SlotClass::Local);
			const LValue* held = ctx.NewLValue(temp);
			steps.push_back(ctx.out->NewNode<StoreNode>(held, node));
			return ctx.out->NewNode<LoadNode>(node->type, held);
		}

		// The getter call of a by-value indexer, reading `receiver` at `index`.
		const HIRNode* ReadAt(LoweringContext& ctx, const LValue* indexer, const LValue* receiver, const HIRNode* index)
		{
			auto* read = ctx.out->NewNode<CallNode>(*static_cast<const CallNode*>(indexer->read));
			read->receiver = receiver;
			if (!read->args.empty()) read->args[0] = index;
			return read;
		}
	}

	const LValue* EvaluatedOnce(LoweringContext& ctx, const LValue* location, std::vector<const HIRNode*>& steps)
	{
		LValue copy = *location;
		switch (location->kind)
		{
		case LValue::Kind::Slot:
		case LValue::Kind::Global:
		case LValue::Kind::This:
			return location;
		case LValue::Kind::Field:
		case LValue::Kind::Base:
		case LValue::Kind::Lane:
		case LValue::Kind::Swizzle:
			copy.base = EvaluatedOnce(ctx, location->base, steps);
			if (copy.base == location->base) return location;
			break;
		case LValue::Kind::VectorLaneDyn:
			copy.base = EvaluatedOnce(ctx, location->base, steps);
			copy.index = Held(ctx, location->index, steps);
			break;
		case LValue::Kind::Deref:
			copy.addr = Held(ctx, location->addr, steps);
			if (copy.addr == location->addr) return location;
			break;
		case LValue::Kind::IndexOp:
			copy.receiver = EvaluatedOnce(ctx, location->receiver, steps);
			copy.idxNode = Held(ctx, location->idxNode, steps);
			copy.read = location->read ? ReadAt(ctx, location, copy.receiver, copy.idxNode) : nullptr;
			break;
		}
		return ctx.NewLValue(copy);
	}

	const HIRNode* AfterSteps(LoweringContext& ctx, const std::vector<const HIRNode*>& steps, const HIRNode* result)
	{
		if (steps.empty()) return result;
		auto* sequence = ctx.out->NewNode<SequenceNode>(result->type);
		sequence->steps = steps;
		sequence->steps.push_back(result);
		return sequence;
	}
}
