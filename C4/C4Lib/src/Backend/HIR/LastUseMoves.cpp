#include "Backend/HIR/LastUseMoves.h"
#include <unordered_set>
#include <vector>

namespace Scripting
{
	namespace
	{
		constexpr uint32 kBeforeEverything = 0xFFFFFFFFu; // a parameter or catch variable begins outside every loop
		constexpr uint32 kNoOwner = 0xFFFFFFFFu;

		struct Interval
		{
			uint32 begin = 0;
			uint32 end = 0;
			bool Contains(uint32 at) const { return at >= begin && at < end; }
		};

		// Where each variable is used, in evaluation order, and what keeps its copies.
		struct UseFacts
		{
			std::unordered_map<uint32, std::vector<uint32>> uses;
			std::unordered_map<uint32, uint32> declaredAt;
			std::unordered_map<uint32, LocalDeclNode*> declarations;
			std::unordered_map<const TransferNode*, uint32> sites;
			std::unordered_set<uint32> escaped;
			std::unordered_set<uint32> usedByCleanup;
			std::vector<Interval> loops;
			std::vector<std::pair<Interval, Interval>> exclusive;
			bool hasGoto = false;
		};

		// The location an address node points at (`&x`, a sequence ending in it), else null.
		const LValue* AddressedLocation(const HIRNode* node)
		{
			if (!node) return nullptr;
			if (node->kind == HIRNode::Kind::AddressOf) return static_cast<const AddressOfNode*>(node)->loc;
			if (node->kind == HIRNode::Kind::Convert) return AddressedLocation(static_cast<const ConvertNode*>(node)->src);
			if (node->kind == HIRNode::Kind::Sequence)
			{
				const auto* seq = static_cast<const SequenceNode*>(node);
				return seq->steps.empty() ? nullptr : AddressedLocation(seq->steps.back());
			}
			return nullptr;
		}

		// The variable a location is part of, or kNoOwner.
		uint32 RootSlot(const LValue* lv)
		{
			while (lv)
			{
				switch (lv->kind)
				{
				case LValue::Kind::Slot: return lv->slot;
				case LValue::Kind::Field: case LValue::Kind::Base: case LValue::Kind::Lane: case LValue::Kind::Swizzle:
				case LValue::Kind::VectorLaneDyn: lv = lv->base; break;
				case LValue::Kind::IndexOp: lv = lv->receiver; break;
				case LValue::Kind::Deref: lv = AddressedLocation(lv->addr); break;
				default: return kNoOwner;
				}
			}
			return kNoOwner;
		}

		// One walk of a function's HIR in evaluation order.
		class UseOrder
		{
		public:
			UseOrder(const LoweringContext& ctx, UseFacts& facts) : m_Facts(facts)
			{
				for (const auto& [slot, cleanup] : ctx.localCleanups) m_OwnCleanup[cleanup.cleanup] = slot;
			}

			void Walk(const HIRNode* n)
			{
				if (!n) return;
				const uint32 at = m_Next++;
				switch (n->kind)
				{
				case HIRNode::Kind::Goto: m_Facts.hasGoto = true; return;
				case HIRNode::Kind::Load: Location(static_cast<const LoadNode*>(n)->loc); return;
				case HIRNode::Kind::Call: Call(*static_cast<const CallNode*>(n)); return;
				case HIRNode::Kind::IntrinsicOp: Walk(static_cast<const IntrinsicOpNode*>(n)->lhs); Walk(static_cast<const IntrinsicOpNode*>(n)->rhs); return;
				case HIRNode::Kind::Convert: Walk(static_cast<const ConvertNode*>(n)->src); return;
				case HIRNode::Kind::New:
				{
					const auto* nw = static_cast<const NewNode*>(n);
					Walk(nw->placement); Walk(nw->count); All(nw->args); Walk(nw->initialize);
					return;
				}
				case HIRNode::Kind::Delete: Walk(static_cast<const DeleteNode*>(n)->operand); return;
				case HIRNode::Kind::Construct: All(static_cast<const ConstructNode*>(n)->args); return;
				case HIRNode::Kind::AddressOf: Address(*static_cast<const AddressOfNode*>(n)); return;
				case HIRNode::Kind::AddressArithmetic:
					Walk(static_cast<const AddressArithmeticNode*>(n)->base); Walk(static_cast<const AddressArithmeticNode*>(n)->offset); return;
				case HIRNode::Kind::Ternary: Ternary(*static_cast<const TernaryNode*>(n)); return;
				case HIRNode::Kind::InitList: All(static_cast<const InitListNode*>(n)->elements); return;
				case HIRNode::Kind::VectorBuild: All(static_cast<const VectorBuildNode*>(n)->elems); return;
				case HIRNode::Kind::Sequence: All(static_cast<const SequenceNode*>(n)->steps); return;
				case HIRNode::Kind::DynamicCast: Walk(static_cast<const DynamicCastNode*>(n)->operand); return;
				case HIRNode::Kind::Relocate:
					Walk(static_cast<const RelocateNode*>(n)->destination); Walk(static_cast<const RelocateNode*>(n)->source); return;
				case HIRNode::Kind::MakeFunc: Walk(static_cast<const MakeFuncNode*>(n)->source); return;
				case HIRNode::Kind::FuncCode: Walk(static_cast<const FuncCodeNode*>(n)->func); return;
				case HIRNode::Kind::Va: Walk(static_cast<const VaNode*>(n)->list); Walk(static_cast<const VaNode*>(n)->source); return;
				case HIRNode::Kind::Atomic:
				{
					const auto* atomic = static_cast<const AtomicNode*>(n);
					Walk(atomic->address); Walk(atomic->expected); Walk(atomic->value);
					Walk(atomic->orderValue); Walk(atomic->failureOrderValue);
					return;
				}
				case HIRNode::Kind::Simd: All(static_cast<const SimdNode*>(n)->operands); return;
				case HIRNode::Kind::Transfer: Site(static_cast<const TransferNode*>(n)); return;
				case HIRNode::Kind::Block: All(static_cast<const BlockNode*>(n)->stmts); return;
				case HIRNode::Kind::If: If(*static_cast<const IfNode*>(n)); return;
				case HIRNode::Kind::While: While(*static_cast<const WhileNode*>(n)); return;
				case HIRNode::Kind::For: For(*static_cast<const ForNode*>(n)); return;
				case HIRNode::Kind::Switch: Switch(*static_cast<const SwitchNode*>(n)); return;
				case HIRNode::Kind::Return: Walk(static_cast<const ReturnNode*>(n)->value); return;
				case HIRNode::Kind::ExprStmt: Walk(static_cast<const ExprStmtNode*>(n)->expr); return;
				case HIRNode::Kind::LocalDecl: Declaration(static_cast<const LocalDeclNode*>(n), at); return;
				case HIRNode::Kind::Once: Walk(static_cast<const OnceNode*>(n)->body); return;
				case HIRNode::Kind::AtExit: Location(static_cast<const AtExitNode*>(n)->object); return;
				case HIRNode::Kind::Asm:
				{
					const auto* asmNode = static_cast<const AsmNode*>(n);
					All(asmNode->inputs);
					for (const LValue* output : asmNode->outputs) Location(output);
					return;
				}
				case HIRNode::Kind::Store: Walk(static_cast<const StoreNode*>(n)->value); Location(static_cast<const StoreNode*>(n)->target); return;
				case HIRNode::Kind::TryCatch: Try(*static_cast<const TryNode*>(n)); return;
				case HIRNode::Kind::Throw: Walk(static_cast<const ThrowNode*>(n)->value); return;
				case HIRNode::Kind::Defer: Cleanup(*static_cast<const DeferNode*>(n)); return;
				default: return; // constants, function addresses, jumps and labels use no variable
				}
			}

		private:
			template <typename Nodes> void All(const Nodes& nodes) { for (const HIRNode* node : nodes) Walk(node); }

			uint32 Use(uint32 slot)
			{
				const uint32 at = m_Next++;
				if (m_InCleanup) { if (slot != m_CleanupOwner) m_Facts.usedByCleanup.insert(slot); }
				else m_Facts.uses[slot].push_back(at);
				return at;
			}

			void Location(const LValue* lv)
			{
				if (!lv) return;
				switch (lv->kind)
				{
				case LValue::Kind::Slot: Use(lv->slot); return;
				case LValue::Kind::Field: case LValue::Kind::Base: case LValue::Kind::Lane: case LValue::Kind::Swizzle: Location(lv->base); return;
				case LValue::Kind::VectorLaneDyn: Location(lv->base); Walk(lv->index); return;
				case LValue::Kind::Deref: Walk(lv->addr); return;
				case LValue::Kind::IndexOp: Location(lv->receiver); Walk(lv->idxNode); Walk(lv->read); return;
				default: return; // `this` and globals
				}
			}

			void Call(const CallNode& call)
			{
				Location(call.receiver);
				Walk(call.target);
				Walk(call.code);
				All(call.args);
			}

			void Address(const AddressOfNode& address)
			{
				Location(address.loc);
				if (address.transient) return;
				if (const uint32 slot = RootSlot(address.loc); slot != kNoOwner) m_Facts.escaped.insert(slot);
			}

			void Site(const TransferNode* transfer)
			{
				// The copy is the use; the copy's own nodes read the same variable and are not walked.
				m_Facts.sites[transfer] = Use(transfer->source->slot);
			}

			void Declaration(const LocalDeclNode* decl, uint32 at)
			{
				Walk(decl->init);
				m_Facts.declaredAt[decl->slot] = at;
				m_Facts.declarations[decl->slot] = const_cast<LocalDeclNode*>(decl); // HIR nodes live in the function's arena
			}

			Interval Span(const HIRNode* node)
			{
				Interval span;
				span.begin = m_Next;
				Walk(node);
				span.end = m_Next;
				return span;
			}

			void Exclusive(const HIRNode* first, const HIRNode* second)
			{
				const Interval a = Span(first);
				const Interval b = Span(second);
				m_Facts.exclusive.push_back({ a, b });
			}

			void Ternary(const TernaryNode& t)
			{
				Walk(t.cond);
				Exclusive(t.thenExpr, t.elseExpr);
			}

			void If(const IfNode& i)
			{
				Walk(i.cond);
				Exclusive(i.thenBlock, i.elseBlock);
			}

			void While(const WhileNode& w)
			{
				Interval loop;
				loop.begin = m_Next;
				if (w.isDoWhile) { Walk(w.body); Walk(w.cond); }
				else { Walk(w.cond); Walk(w.body); }
				loop.end = m_Next;
				m_Facts.loops.push_back(loop);
			}

			void For(const ForNode& f)
			{
				Walk(f.initBlock);
				Interval loop;
				loop.begin = m_Next;
				Walk(f.cond);
				Walk(f.body);
				Walk(f.step);
				loop.end = m_Next;
				m_Facts.loops.push_back(loop);
			}

			void Switch(const SwitchNode& s)
			{
				Walk(s.cond);
				for (const SwitchCase& c : s.cases) Walk(c.block);
				Walk(s.defaultBlock);
			}

			void Try(const TryNode& t)
			{
				Walk(t.tryBlock);
				for (const TryNode::CatchClause& c : t.catches) Walk(c.block);
			}

			void Cleanup(const DeferNode& defer)
			{
				// A cleanup runs when its scope ends: a variable it uses (other than the one it destroys) keeps its copies.
				const bool outerIn = m_InCleanup;
				const uint32 outerOwner = m_CleanupOwner;
				auto own = m_OwnCleanup.find(&defer);
				m_InCleanup = true;
				m_CleanupOwner = own != m_OwnCleanup.end() ? own->second : kNoOwner;
				Walk(defer.body);
				m_InCleanup = outerIn;
				m_CleanupOwner = outerOwner;
			}

			UseFacts& m_Facts;
			std::unordered_map<const DeferNode*, uint32> m_OwnCleanup;
			uint32 m_Next = 0;
			bool m_InCleanup = false;
			uint32 m_CleanupOwner = kNoOwner;
		};

		bool InOtherArm(const UseFacts& facts, uint32 at, uint32 use)
		{
			for (const auto& [a, b] : facts.exclusive)
				if ((a.Contains(at) && b.Contains(use)) || (b.Contains(at) && a.Contains(use))) return true;
			return false;
		}

		bool IsLastUse(const UseFacts& facts, uint32 slot, uint32 at)
		{
			for (uint32 use : facts.uses.at(slot))
				if (use > at && !InOtherArm(facts, at, use)) return false;
			auto declared = facts.declaredAt.find(slot);
			const uint32 decl = declared != facts.declaredAt.end() ? declared->second : kBeforeEverything;
			for (const Interval& loop : facts.loops)
				if (loop.Contains(at) && !loop.Contains(decl)) return false;
			return true;
		}

		class MovePlan
		{
		public:
			MovePlan(LoweringContext& ctx, HIRFunction& fn, const UseFacts& facts) : m_Ctx(ctx), m_Fn(fn), m_Facts(facts) {}

			bool Movable(const LoweringContext::TransferSite& site) const
			{
				auto at = m_Facts.sites.find(site.node);
				if (at == m_Facts.sites.end() || m_Facts.escaped.count(site.slot) || m_Facts.usedByCleanup.count(site.slot)) return false;
				auto cleanup = m_Ctx.localCleanups.find(site.slot);
				const bool begins = cleanup == m_Ctx.localCleanups.end() || cleanup->second.beginsWith || m_Facts.declarations.count(site.slot);
				return begins && IsLastUse(m_Facts, site.slot, at->second);
			}

			void Move(const LoweringContext::TransferSite& site)
			{
				// The object's bytes, and the variable is no longer the one to destroy it.
				TransferNode* node = site.node;
				node->move = true;
				node->dropFlag = FlagFor(site.slot);
				auto* moved = m_Fn.NewNode<SequenceNode>(node->type);
				if (node->dropFlag != kNoDropFlag) moved->steps.push_back(m_Fn.NewNode<StoreNode>(Flag(node->dropFlag), Bool(false)));
				moved->steps.push_back(m_Fn.NewNode<LoadNode>(node->type, node->source));
				node->copy = moved;
			}

		private:
			uint32 FlagFor(uint32 slot)
			{
				auto cleanup = m_Ctx.localCleanups.find(slot);
				if (cleanup == m_Ctx.localCleanups.end()) return kNoDropFlag; // nothing destroys it
				if (auto made = m_Flags.find(slot); made != m_Flags.end()) return made->second;
				const uint32 flag = m_Fn.AddSlot(BoolType(), "__live_" + m_Fn.GetSlot(slot).name, SlotClass::Local);
				m_Flags[slot] = flag;
				DeferNode* defer = cleanup->second.cleanup;
				auto* guard = m_Fn.NewNode<IfNode>(m_Fn.NewNode<LoadNode>(BoolType(), Flag(flag)));
				guard->thenBlock = defer->body;
				defer->body = m_Fn.NewNode<BlockNode>(nullptr);
				defer->body->stmts.push_back(guard);
				if (BlockNode* begins = cleanup->second.beginsWith)
					begins->stmts.insert(begins->stmts.begin(), m_Fn.NewNode<StoreNode>(Flag(flag), Bool(true)));
				else m_Facts.declarations.at(slot)->liveFlag = flag;
				return flag;
			}

			const Type* BoolType() const { return m_Ctx.queries().TypeStore()->Builtin(TypeKind::Bool); }
			const HIRNode* Bool(bool value) { return m_Fn.NewNode<ConstantNode>(BoolType(), ConstantValue::Bool(value)); }
			const LValue* Flag(uint32 flag)
			{
				LValue lv; lv.kind = LValue::Kind::Slot; lv.type = BoolType(); lv.slot = flag;
				return m_Fn.NewLValue(lv);
			}

			LoweringContext& m_Ctx;
			HIRFunction& m_Fn;
			const UseFacts& m_Facts;
			std::unordered_map<uint32, uint32> m_Flags;
		};
	}

	void MoveAtLastUse(LoweringContext& ctx, HIRFunction& fn)
	{
		if (ctx.transfers.empty() || !fn.root) return;
		UseFacts facts;
		UseOrder(ctx, facts).Walk(fn.root);
		if (facts.hasGoto) return;
		MovePlan plan(ctx, fn, facts);
		for (const LoweringContext::TransferSite& site : ctx.transfers)
			if (plan.Movable(site)) plan.Move(site);
	}
} // namespace Scripting
