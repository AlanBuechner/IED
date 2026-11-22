#include "Backend/MIR/MirMoves.h"
#include "Backend/MIR/MirReach.h"
#include <unordered_set>

namespace C4Compiler
{
	MirBuildOptions MirMoveSites::BuildOptions() const
	{
		MirBuildOptions options;
		for (const MirTransfer& transfer : transfers)
		{
			auto cleanup = cleanups.find(transfer.slot);
			if (cleanup == cleanups.end()) continue;
			options.guarded.insert(cleanup->second.cleanup);
			if (cleanup->second.beginsWith) options.noted.insert(cleanup->second.beginsWith);
		}
		return options;
	}

	namespace
	{
		constexpr uint32 c_NotCandidate = 0xFFFFFFFFu;

		// --- the flow of the candidates' uses -----------------------------------------------------------------------
		// A segment is a run of a block's code with no control flow inside: a block's code, cut where a statement's
		// expression runs a region (the region's blocks come between the two segments). Its events are the uses,
		// copies and declarations of the variables that have copies, in evaluation order.

		struct Event
		{
			enum class Kind : uint8 { Use, Declare, Site };
			Kind kind = Kind::Use;
			uint32 candidate = 0;
			const TransferNode* site = nullptr;
		};

		struct Interval
		{
			uint32 begin = 0;
			uint32 end = 0;
			bool Contains(uint32 at) const { return at >= begin && at < end; }
		};

		struct Segment
		{
			std::vector<Event> events;
			std::vector<std::pair<Interval, Interval>> exclusive; // a `?:`'s two arms, by event
			std::vector<uint32> successors;
		};

		struct SiteAt { uint32 segment = 0; uint32 event = 0; };

		class Bits
		{
		public:
			explicit Bits(size_t count = 0) : m_Words((count + 63) / 64, 0) {}
			void Set(uint32 i) { m_Words[i / 64] |= uint64(1) << (i % 64); }
			bool Test(uint32 i) const { return (m_Words[i / 64] >> (i % 64)) & 1; }
			bool operator==(const Bits& other) const { return m_Words == other.m_Words; }
			void Or(const Bits& other) { for (size_t i = 0; i < m_Words.size(); ++i) m_Words[i] |= other.m_Words[i]; }
			/// this = gen | (out & ~kill)
			void Transfer(const Bits& gen, const Bits& out, const Bits& kill)
			{
				for (size_t i = 0; i < m_Words.size(); ++i) m_Words[i] = gen.m_Words[i] | (out.m_Words[i] & ~kill.m_Words[i]);
			}
		private:
			std::vector<uint64> m_Words;
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

		// The variables a location may be part of, into `slots`: both branches' of a choice between locations (BUG-453).
		void AddRootSlots(const LValue* lv, std::unordered_set<uint32>& slots)
		{
			while (lv)
			{
				switch (lv->kind)
				{
				case LValue::Kind::Slot: slots.insert(lv->slot); return;
				case LValue::Kind::Field: case LValue::Kind::Base: case LValue::Kind::Lane: case LValue::Kind::Swizzle:
				case LValue::Kind::VectorLaneDyn: lv = lv->base; break;
				case LValue::Kind::IndexOp: lv = lv->receiver; break;
				case LValue::Kind::Deref:
					if (lv->addr && lv->addr->kind == HIRNode::Kind::Ternary)
					{
						const auto* choice = static_cast<const TernaryNode*>(lv->addr);
						AddRootSlots(AddressedLocation(choice->thenExpr), slots);
						lv = AddressedLocation(choice->elseExpr);
					}
					else lv = AddressedLocation(lv->addr);
					break;
				default: return;
				}
			}
		}

		/// The flow graph of the candidates' uses over a function's MIR, and what keeps a candidate's copies (its address
		/// escaped, another cleanup uses it).
		class UseFlow
		{
		public:
			UseFlow(const MirBody& mir, const std::unordered_map<uint32, uint32>& candidates,
				const std::unordered_map<const DeferNode*, uint32>& owners)
				: m_Mir(mir), m_Candidates(candidates), m_Owners(owners)
			{
			}

			void Build()
			{
				m_First.assign(m_Mir.blocks.size(), 0);
				m_Last.assign(m_Mir.blocks.size(), 0);
				for (MirBlockId b = 0; b < m_Mir.blocks.size(); ++b) Walk(b);
				Connect();
			}

			std::vector<Segment> segments;
			std::unordered_set<uint32> escaped;      // slots
			std::unordered_set<uint32> keptByCleanup; // candidates another cleanup uses
			std::unordered_map<const TransferNode*, std::vector<SiteAt>> sites;
			std::unordered_set<const TransferNode*> sitesInCleanups;
			std::unordered_set<uint32> declared;      // slots a Declare starts

		private:
			uint32 NewSegment()
			{
				segments.emplace_back();
				return static_cast<uint32>(segments.size() - 1);
			}

			void Walk(MirBlockId b)
			{
				const MirBlock& block = m_Mir.blocks[b];
				m_InCleanup = block.cleanupOf != nullptr;
				auto owner = m_InCleanup ? m_Owners.find(block.cleanupOf) : m_Owners.end();
				m_Owner = owner != m_Owners.end() ? owner->second : c_NotCandidate;
				m_Segment = NewSegment();
				m_First[b] = m_Segment;
				for (const MirStatement& statement : block.statements)
				{
					m_Regions = { statement.firstRegion, statement.regionCount };
					Statement(statement);
				}
				m_Regions = { block.end.firstRegion, block.end.regionCount };
				Terminator(block.end);
				m_Last[b] = m_Segment;
			}

			void Statement(const MirStatement& statement)
			{
				switch (statement.kind)
				{
				case MirStatement::Kind::Eval:
				case MirStatement::Kind::StageReturn: Node(statement.node); return;
				case MirStatement::Kind::Declare:
				{
					const auto* declaration = static_cast<const LocalDeclNode*>(statement.node);
					Node(declaration->init);
					declared.insert(declaration->slot);
					Declare(declaration->slot);
					return;
				}
				case MirStatement::Kind::Asm:
				{
					const auto* block = static_cast<const AsmNode*>(statement.node);
					for (const HIRNode* input : block->inputs) Node(input);
					for (const LValue* output : block->outputs) Location(output);
					return;
				}
				case MirStatement::Kind::AtExit: Location(static_cast<const AtExitNode*>(statement.node)->object); return;
				default: return; // a drop flag, a once region's end
				}
			}

			void Terminator(const MirTerminator& end)
			{
				switch (end.kind)
				{
				case MirTerminator::Kind::Branch:
				case MirTerminator::Kind::Switch:
				case MirTerminator::Kind::Return: Node(end.node); return;
				case MirTerminator::Kind::Throw: Node(static_cast<const ThrowNode*>(end.node)->value); return;
				default: return;
				}
			}

			template <typename Nodes> void All(const Nodes& nodes) { for (const HIRNode* node : nodes) Node(node); }

			void Node(const HIRNode* n)
			{
				if (!n) return;
				switch (n->kind)
				{
				case HIRNode::Kind::Load: Location(static_cast<const LoadNode*>(n)->loc); return;
				case HIRNode::Kind::Call:
				{
					const auto* call = static_cast<const CallNode*>(n);
					Location(call->receiver);
					Node(call->target);
					Node(call->code);
					All(call->args);
					return;
				}
				case HIRNode::Kind::IntrinsicOp: Node(static_cast<const IntrinsicOpNode*>(n)->lhs); Node(static_cast<const IntrinsicOpNode*>(n)->rhs); return;
				case HIRNode::Kind::Convert: Node(static_cast<const ConvertNode*>(n)->src); return;
				case HIRNode::Kind::New:
				{
					const auto* made = static_cast<const NewNode*>(n);
					Node(made->placement);
					Node(made->count);
					All(made->args);
					Region(made->initialize);
					return;
				}
				case HIRNode::Kind::Delete: Node(static_cast<const DeleteNode*>(n)->operand); return;
				case HIRNode::Kind::Construct: All(static_cast<const ConstructNode*>(n)->args); return;
				case HIRNode::Kind::AddressOf:
				{
					const auto* address = static_cast<const AddressOfNode*>(n);
					Location(address->loc);
					if (!address->transient) AddRootSlots(address->loc, escaped);
					return;
				}
				case HIRNode::Kind::AddressArithmetic:
					Node(static_cast<const AddressArithmeticNode*>(n)->base); Node(static_cast<const AddressArithmeticNode*>(n)->offset); return;
				case HIRNode::Kind::Ternary:
				{
					const auto* choice = static_cast<const TernaryNode*>(n);
					Node(choice->cond);
					Exclusive(choice->thenExpr, choice->elseExpr);
					return;
				}
				case HIRNode::Kind::InitList: All(static_cast<const InitListNode*>(n)->elements); return;
				case HIRNode::Kind::VectorBuild: All(static_cast<const VectorBuildNode*>(n)->elems); return;
				case HIRNode::Kind::Sequence:
					for (const HIRNode* step : static_cast<const SequenceNode*>(n)->steps)
						if (step && step->kind == HIRNode::Kind::Block) Region(static_cast<const BlockNode*>(step));
						else Node(step);
					return;
				case HIRNode::Kind::DynamicCast: Node(static_cast<const DynamicCastNode*>(n)->operand); return;
				case HIRNode::Kind::Relocate: Node(static_cast<const RelocateNode*>(n)->destination); Node(static_cast<const RelocateNode*>(n)->source); return;
				case HIRNode::Kind::MakeFunc: Node(static_cast<const MakeFuncNode*>(n)->source); return;
				case HIRNode::Kind::FuncCode: Node(static_cast<const FuncCodeNode*>(n)->func); return;
				case HIRNode::Kind::Va: Node(static_cast<const VaNode*>(n)->list); Node(static_cast<const VaNode*>(n)->source); return;
				case HIRNode::Kind::Atomic:
				{
					const auto* atomic = static_cast<const AtomicNode*>(n);
					Node(atomic->address); Node(atomic->expected); Node(atomic->value);
					Node(atomic->orderValue); Node(atomic->failureOrderValue);
					return;
				}
				case HIRNode::Kind::Simd: All(static_cast<const SimdNode*>(n)->operands); return;
				case HIRNode::Kind::Transfer: Site(static_cast<const TransferNode*>(n)); return;
				case HIRNode::Kind::Store: Node(static_cast<const StoreNode*>(n)->value); Location(static_cast<const StoreNode*>(n)->target); return;
				case HIRNode::Kind::Block: Region(static_cast<const BlockNode*>(n)); return;
				default: return; // constants, function addresses
				}
			}

			void Location(const LValue* lv)
			{
				if (!lv) return;
				switch (lv->kind)
				{
				case LValue::Kind::Slot: Use(lv->slot); return;
				case LValue::Kind::Field: case LValue::Kind::Base: case LValue::Kind::Lane: case LValue::Kind::Swizzle: Location(lv->base); return;
				case LValue::Kind::VectorLaneDyn: Location(lv->base); Node(lv->index); return;
				case LValue::Kind::Deref: Node(lv->addr); return;
				case LValue::Kind::IndexOp: Location(lv->receiver); Node(lv->idxNode); Node(lv->read); return;
				default: return; // `this` and globals
				}
			}

			uint32 Candidate(uint32 slot) const
			{
				auto it = m_Candidates.find(slot);
				return it != m_Candidates.end() ? it->second : c_NotCandidate;
			}

			void Use(uint32 slot)
			{
				const uint32 candidate = Candidate(slot);
				if (candidate == c_NotCandidate) return;
				if (m_InCleanup)
				{
					if (slot != m_Owner) keptByCleanup.insert(candidate);
					return;
				}
				segments[m_Segment].events.push_back({ Event::Kind::Use, candidate, nullptr });
			}

			void Declare(uint32 slot)
			{
				const uint32 candidate = Candidate(slot);
				if (candidate == c_NotCandidate || m_InCleanup) return;
				segments[m_Segment].events.push_back({ Event::Kind::Declare, candidate, nullptr });
			}

			void Site(const TransferNode* transfer)
			{
				// The copy is the use; the copy's own nodes read the same variable and are not walked.
				const uint32 slot = transfer->source->slot;
				const uint32 candidate = Candidate(slot);
				if (candidate == c_NotCandidate) return;
				if (m_InCleanup)
				{
					if (slot != m_Owner) keptByCleanup.insert(candidate);
					sitesInCleanups.insert(transfer);
					return;
				}
				Segment& segment = segments[m_Segment];
				sites[transfer].push_back({ m_Segment, static_cast<uint32>(segment.events.size()) });
				segment.events.push_back({ Event::Kind::Site, candidate, transfer });
			}

			void Exclusive(const HIRNode* first, const HIRNode* second)
			{
				const uint32 segment = m_Segment;
				Interval a{ static_cast<uint32>(segments[segment].events.size()), 0 };
				Node(first);
				a.end = static_cast<uint32>(segments[m_Segment].events.size());
				Interval b{ static_cast<uint32>(segments[m_Segment].events.size()), 0 };
				Node(second);
				b.end = static_cast<uint32>(segments[m_Segment].events.size());
				if (m_Segment == segment) segments[segment].exclusive.push_back({ a, b }); // an arm that ran a region is not set apart
			}

			// The expression runs a region here: its blocks come between this segment and the next.
			void Region(const BlockNode* block)
			{
				if (!block) return;
				for (uint32 i = 0; i < m_Regions.second; ++i)
				{
					const MirRegion& region = m_Mir.regions[m_Regions.first + i];
					if (region.block != block) continue;
					const uint32 before = m_Segment;
					m_Segment = NewSegment();
					m_IntoRegions.push_back({ before, region.entry });
					if (region.exit != c_MirNone) m_OutOfRegions.push_back({ region.exit, m_Segment });
					return;
				}
			}

			void Connect()
			{
				std::vector<MirBlockId> next;
				for (MirBlockId b = 0; b < m_Mir.blocks.size(); ++b)
				{
					const MirBlock& block = m_Mir.blocks[b];
					next.clear();
					m_Mir.ForEachSuccessor(block, [&](MirBlockId successor) { next.push_back(successor); });
					if (block.end.kind == MirTerminator::Kind::Unwind) MirReach::StepTargets(m_Mir, block.end.step, next);
					for (MirBlockId successor : next) segments[m_Last[b]].successors.push_back(m_First[successor]);
					if (!block.mayUnwind) continue;
					next.clear();
					MirReach::StepTargets(m_Mir, block.unwind, next);
					// An exception may come from any of the block's segments.
					for (uint32 s = m_First[b]; s <= m_Last[b]; ++s)
						for (MirBlockId target : next) segments[s].successors.push_back(m_First[target]);
				}
				for (const auto& [segment, entry] : m_IntoRegions) segments[segment].successors.push_back(m_First[entry]);
				for (const auto& [exit, segment] : m_OutOfRegions) segments[m_Last[exit]].successors.push_back(segment);
			}

			const MirBody& m_Mir;
			const std::unordered_map<uint32, uint32>& m_Candidates;
			const std::unordered_map<const DeferNode*, uint32>& m_Owners;
			std::vector<uint32> m_First;
			std::vector<uint32> m_Last;
			std::vector<std::pair<uint32, MirBlockId>> m_IntoRegions;
			std::vector<std::pair<MirBlockId, uint32>> m_OutOfRegions;
			std::pair<uint32, uint32> m_Regions{ 0, 0 };
			uint32 m_Segment = 0;
			bool m_InCleanup = false;
			uint32 m_Owner = c_NotCandidate;
		};

		/// The candidates live after each segment: used on some path from its end before they are declared again.
		std::vector<Bits> LiveOut(const std::vector<Segment>& segments, size_t candidates)
		{
			const size_t count = segments.size();
			std::vector<Bits> gen(count, Bits(candidates)), kill(count, Bits(candidates));
			std::vector<Bits> in(count, Bits(candidates)), out(count, Bits(candidates));
			std::vector<std::vector<uint32>> predecessors(count);
			for (uint32 s = 0; s < count; ++s)
			{
				for (const Event& event : segments[s].events)
				{
					if (event.kind == Event::Kind::Declare) kill[s].Set(event.candidate);
					else if (!kill[s].Test(event.candidate)) gen[s].Set(event.candidate);
				}
				for (uint32 next : segments[s].successors) predecessors[next].push_back(s);
				in[s] = gen[s];
			}
			std::vector<uint32> work;
			std::vector<bool> queued(count, true);
			for (uint32 s = static_cast<uint32>(count); s-- > 0; ) work.push_back(s);
			while (!work.empty())
			{
				const uint32 s = work.back();
				work.pop_back();
				queued[s] = false;
				Bits after(candidates);
				for (uint32 next : segments[s].successors) after.Or(in[next]);
				out[s] = after;
				Bits before(candidates);
				before.Transfer(gen[s], after, kill[s]);
				if (before == in[s]) continue;
				in[s] = before;
				for (uint32 p : predecessors[s])
					if (!queued[p])
					{
						queued[p] = true;
						work.push_back(p);
					}
			}
			return out;
		}

		bool InOtherArm(const Segment& segment, uint32 at, uint32 use)
		{
			for (const auto& [a, b] : segment.exclusive)
				if ((a.Contains(at) && b.Contains(use)) || (b.Contains(at) && a.Contains(use))) return true;
			return false;
		}

		bool IsLastUse(const std::vector<Segment>& segments, const std::vector<Bits>& liveOut, const SiteAt& site, uint32 candidate)
		{
			const Segment& segment = segments[site.segment];
			for (uint32 j = site.event + 1; j < segment.events.size(); ++j)
			{
				const Event& event = segment.events[j];
				if (event.candidate != candidate) continue;
				if (event.kind == Event::Kind::Declare) return true;
				if (!InOtherArm(segment, site.event, j)) return false;
			}
			return !liveOut[site.segment].Test(candidate);
		}

		class MovePlan
		{
		public:
			MovePlan(HIRFunction& fn, MirBody& mir, const MirMoveSites& sites, const Type* boolType)
				: m_Fn(fn), m_Mir(mir), m_Sites(sites), m_Bool(boolType)
			{
			}

			void Move(const MirTransfer& site)
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
				auto cleanup = m_Sites.cleanups.find(slot);
				if (cleanup == m_Sites.cleanups.end()) return kNoDropFlag; // nothing destroys it
				if (auto made = m_Flags.find(slot); made != m_Flags.end()) return made->second;
				const uint32 flag = m_Fn.AddSlot(m_Bool, "__live_" + m_Fn.GetSlot(slot).name, SlotClass::Local);
				m_Mir.declared.push_back(false); // zeroed at entry: not live until the variable begins
				m_Flags[slot] = flag;
				GuardCopies(cleanup->second.cleanup, flag);
				if (cleanup->second.beginsWith) SetAtStart(cleanup->second.beginsWith, flag);
				else SetAfterDeclarations(slot, flag);
				return flag;
			}

			// Each copy of the variable's cleanup runs only while its flag is set.
			void GuardCopies(const DeferNode* cleanup, uint32 flag)
			{
				const HIRNode* live = m_Fn.NewNode<LoadNode>(m_Bool, Flag(flag));
				for (const MirDropGuard& guard : m_Mir.dropGuards)
				{
					if (guard.cleanup != cleanup) continue;
					MirTerminator& end = m_Mir.blocks[guard.guard].end;
					end.kind = MirTerminator::Kind::Branch;
					end.node = live;
					end.target = guard.body;
					end.otherwise = guard.after;
				}
			}

			void SetAtStart(const BlockNode* block, uint32 flag)
			{
				for (const MirBlockStart& start : m_Mir.starts)
				{
					if (start.block != block) continue;
					std::vector<MirStatement>& statements = m_Mir.blocks[start.at].statements;
					const uint32 at = std::min<uint32>(start.statement, static_cast<uint32>(statements.size()));
					MirStatement set = SetFlag(flag);
					set.at = at < statements.size() ? statements[at].at : m_Mir.blocks[start.at].end.at;
					statements.insert(statements.begin() + at, set);
				}
			}

			void SetAfterDeclarations(uint32 slot, uint32 flag)
			{
				for (MirBlock& block : m_Mir.blocks)
					for (size_t i = 0; i < block.statements.size(); ++i)
					{
						const MirStatement& statement = block.statements[i];
						if (statement.kind != MirStatement::Kind::Declare || static_cast<const LocalDeclNode*>(statement.node)->slot != slot) continue;
						MirStatement set = SetFlag(flag);
						set.at = statement.at;
						block.statements.insert(block.statements.begin() + static_cast<std::ptrdiff_t>(i) + 1, set);
						++i;
					}
			}

			static MirStatement SetFlag(uint32 flag)
			{
				MirStatement set;
				set.kind = MirStatement::Kind::SetFlag;
				set.slot = flag;
				set.flag = true;
				return set;
			}

			const HIRNode* Bool(bool value) { return m_Fn.NewNode<ConstantNode>(m_Bool, ConstantValue::Bool(value)); }
			const LValue* Flag(uint32 flag)
			{
				LValue lv; lv.kind = LValue::Kind::Slot; lv.type = m_Bool; lv.slot = flag;
				return m_Fn.NewLValue(lv);
			}

			HIRFunction& m_Fn;
			MirBody& m_Mir;
			const MirMoveSites& m_Sites;
			const Type* m_Bool;
			std::unordered_map<uint32, uint32> m_Flags;
		};
	}

	void MirMoves::Apply(HIRFunction& fn, MirBody& mir, const MirMoveSites& sites, const Type* boolType)
	{
		if (sites.transfers.empty()) return;
		std::unordered_map<uint32, uint32> candidates;
		for (const MirTransfer& transfer : sites.transfers) candidates.emplace(transfer.slot, static_cast<uint32>(candidates.size()));
		std::unordered_map<const DeferNode*, uint32> owners;
		for (const auto& [slot, cleanup] : sites.cleanups) owners[cleanup.cleanup] = slot;

		UseFlow flow(mir, candidates, owners);
		flow.Build();
		const std::vector<Bits> liveOut = LiveOut(flow.segments, candidates.size());

		const auto movable = [&](const MirTransfer& site) {
			auto at = flow.sites.find(site.node);
			if (at == flow.sites.end() || flow.sitesInCleanups.count(site.node)) return false;
			const uint32 candidate = candidates.at(site.slot);
			if (flow.escaped.count(site.slot) || flow.keptByCleanup.count(candidate)) return false;
			auto cleanup = sites.cleanups.find(site.slot);
			const bool begins = cleanup == sites.cleanups.end() || cleanup->second.beginsWith || flow.declared.count(site.slot);
			if (!begins) return false;
			for (const SiteAt& place : at->second)
				if (!IsLastUse(flow.segments, liveOut, place, candidate)) return false;
			return true;
		};
		std::vector<const MirTransfer*> moves;
		for (const MirTransfer& site : sites.transfers)
			if (movable(site)) moves.push_back(&site);
		MovePlan plan(fn, mir, sites, boolType);
		for (const MirTransfer* site : moves) plan.Move(*site);
	}
} // namespace C4Compiler
