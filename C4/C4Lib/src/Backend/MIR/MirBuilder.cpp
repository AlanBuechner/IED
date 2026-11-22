#include "Backend/MIR/MirBuilder.h"
#include <algorithm>

namespace C4Compiler
{
	namespace
	{
		// A loop condition that always holds (`for (;;)`, `while (1)`): such a loop is left only by a jump.
		bool AlwaysTrue(const HIRNode* condition)
		{
			if (!condition) return true;
			const auto* constant = condition->kind == HIRNode::Kind::Constant ? static_cast<const ConstantNode*>(condition) : nullptr;
			return constant && constant->value.AsInt() != 0;
		}

		// The blocks an expression runs in place, in the order codegen emits its operands (HIRWalk's): a Sequence's
		// block steps and `new T[n]`'s start. The statements inside them are their regions' own.
		class RegionFinder
		{
		public:
			explicit RegionFinder(std::vector<const BlockNode*>& found) : m_Found(found) {}

			void Node(const HIRNode* node)
			{
				if (!node) return;
				switch (node->kind)
				{
				case HIRNode::Kind::Load: Place(static_cast<const LoadNode*>(node)->loc); return;
				case HIRNode::Kind::Call:
				{
					const auto* call = static_cast<const CallNode*>(node);
					Place(call->receiver);
					Node(call->target);
					Node(call->code);
					Each(call->args);
					return;
				}
				case HIRNode::Kind::IntrinsicOp: Node(static_cast<const IntrinsicOpNode*>(node)->lhs); Node(static_cast<const IntrinsicOpNode*>(node)->rhs); return;
				case HIRNode::Kind::Convert: Node(static_cast<const ConvertNode*>(node)->src); return;
				case HIRNode::Kind::New:
				{
					const auto* made = static_cast<const NewNode*>(node);
					Node(made->placement);
					Node(made->count);
					Each(made->args);
					Place(made->elements);
					Region(made->initialize);
					return;
				}
				case HIRNode::Kind::Delete: Node(static_cast<const DeleteNode*>(node)->operand); return;
				case HIRNode::Kind::Construct: Each(static_cast<const ConstructNode*>(node)->args); return;
				case HIRNode::Kind::AddressOf: Place(static_cast<const AddressOfNode*>(node)->loc); return;
				case HIRNode::Kind::AddressArithmetic:
					Node(static_cast<const AddressArithmeticNode*>(node)->base); Node(static_cast<const AddressArithmeticNode*>(node)->offset); return;
				case HIRNode::Kind::Ternary:
				{
					const auto* choice = static_cast<const TernaryNode*>(node);
					Node(choice->cond);
					Node(choice->thenExpr);
					Node(choice->elseExpr);
					return;
				}
				case HIRNode::Kind::InitList: Each(static_cast<const InitListNode*>(node)->elements); return;
				case HIRNode::Kind::VectorBuild: Each(static_cast<const VectorBuildNode*>(node)->elems); return;
				case HIRNode::Kind::Sequence:
					for (const HIRNode* step : static_cast<const SequenceNode*>(node)->steps)
						if (step && step->kind == HIRNode::Kind::Block) Region(static_cast<const BlockNode*>(step));
						else Node(step);
					return;
				case HIRNode::Kind::DynamicCast: Node(static_cast<const DynamicCastNode*>(node)->operand); return;
				case HIRNode::Kind::Relocate: Node(static_cast<const RelocateNode*>(node)->destination); Node(static_cast<const RelocateNode*>(node)->source); return;
				case HIRNode::Kind::MakeFunc: Node(static_cast<const MakeFuncNode*>(node)->source); return;
				case HIRNode::Kind::FuncCode: Node(static_cast<const FuncCodeNode*>(node)->func); return;
				case HIRNode::Kind::Transfer: Node(static_cast<const TransferNode*>(node)->copy); return;
				case HIRNode::Kind::Va: Node(static_cast<const VaNode*>(node)->list); Node(static_cast<const VaNode*>(node)->source); return;
				case HIRNode::Kind::Atomic:
				{
					const auto* atomic = static_cast<const AtomicNode*>(node);
					Node(atomic->orderValue);
					Node(atomic->failureOrderValue);
					Node(atomic->address);
					Node(atomic->value);
					Node(atomic->expected);
					return;
				}
				case HIRNode::Kind::Simd: Each(static_cast<const SimdNode*>(node)->operands); return;
				case HIRNode::Kind::Store: Place(static_cast<const StoreNode*>(node)->target); Node(static_cast<const StoreNode*>(node)->value); return;
				case HIRNode::Kind::Block: Region(static_cast<const BlockNode*>(node)); return;
				default: return; // constants, function addresses
				}
			}

			void Place(const LValue* place)
			{
				while (place)
				{
					switch (place->kind)
					{
					case LValue::Kind::Field: case LValue::Kind::Base: case LValue::Kind::Lane: case LValue::Kind::Swizzle:
						place = place->base;
						break;
					case LValue::Kind::VectorLaneDyn:
						Node(place->index);
						place = place->base;
						break;
					case LValue::Kind::Deref: Node(place->addr); return;
					case LValue::Kind::IndexOp: Node(place->read); return;
					default: return;
					}
				}
			}

		private:
			template <typename Nodes> void Each(const Nodes& nodes) { for (const HIRNode* node : nodes) Node(node); }
			void Region(const BlockNode* block)
			{
				if (block && std::find(m_Found.begin(), m_Found.end(), block) == m_Found.end()) m_Found.push_back(block);
			}

			std::vector<const BlockNode*>& m_Found;
		};
	}

	MirBuilder::MirBuilder(const HIRFunction& hir, const MirBuildOptions* options) : m_Hir(hir), m_Options(options)
	{
		m_Body.declared.assign(hir.SlotCount(), false);
	}

	MirBody MirBuilder::Build()
	{
		if (!m_Hir.root) return std::move(m_Body);
		Enter(NewBlock());
		if (m_Hir.record && m_Hir.record->isNoexcept) BuildNoexceptBody(m_Hir.root);
		else BuildBlock(m_Hir.root);
		m_Body.fallOff = m_Current;
		End({ MirTerminator::Kind::FallOff });
		return std::move(m_Body);
	}

	// --- blocks -----------------------------------------------------------------------------------------------

	MirBlockId MirBuilder::NewBlock()
	{
		m_Body.blocks.emplace_back();
		return static_cast<MirBlockId>(m_Body.blocks.size() - 1);
	}

	void MirBuilder::Enter(MirBlockId block)
	{
		m_Current = block;
		MirBlock& entered = m_Body.blocks[block];
		entered.funclet = CurrentFunclet();
		entered.cleanupOf = m_CleanupOf;
	}

	void MirBuilder::MatchScopes()
	{
		const MirStepId unwind = UnwindTarget();
		const MirFuncletId funclet = CurrentFunclet();
		MirBlock& block = Current();
		if (block.mayUnwind && block.unwind == unwind && block.funclet == funclet && block.cleanupOf == m_CleanupOf) return;
		if (block.mayUnwind || !block.statements.empty())
		{
			// The block ran code in other scopes: what follows goes on in a block of its own.
			const MirBlockId next = NewBlock();
			Goto(next);
			Enter(next);
		}
		MirBlock& matched = Current();
		matched.mayUnwind = true;
		matched.unwind = unwind;
		matched.funclet = funclet;
		matched.cleanupOf = m_CleanupOf;
	}

	void MirBuilder::Add(MirStatement statement)
	{
		if (!IsOpen()) ContinueAfterTerminator();
		MatchScopes();
		statement.at = m_At;
		if (statement.kind == MirStatement::Kind::Declare)
		{
			const auto* declaration = static_cast<const LocalDeclNode*>(statement.node);
			if (m_Hir.GetSlot(declaration->slot).cls != SlotClass::Static) m_Body.declared[declaration->slot] = true;
		}
		Current().statements.push_back(statement);
	}

	void MirBuilder::End(MirTerminator end)
	{
		end.at = m_At;
		Current().end = end;
	}

	void MirBuilder::EndWith(MirTerminator end)
	{
		if (!IsOpen()) ContinueAfterTerminator();
		if (end.node || end.kind == MirTerminator::Kind::Throw) MatchScopes();
		End(end);
	}

	void MirBuilder::Goto(MirBlockId target)
	{
		MirTerminator end;
		end.kind = MirTerminator::Kind::Goto;
		end.target = target;
		End(end);
	}

	void MirBuilder::ContinueAfterTerminator()
	{
		Enter(NewBlock());
	}

	MirBuilder::Place MirBuilder::Save() const
	{
		return { m_Current, m_At, m_CleanupOf, m_Floor, m_FloorFunclet };
	}

	void MirBuilder::Restore(const Place& place)
	{
		m_Current = place.current;
		m_At = place.at;
		m_CleanupOf = place.cleanupOf;
		m_Floor = place.floor;
		m_FloorFunclet = place.floorFunclet;
	}

	// --- statements -------------------------------------------------------------------------------------------

	void MirBuilder::BuildBlock(const BlockNode* block)
	{
		const size_t start = m_Scopes.size();
		m_Frames.push_back({ block, start });
		DeclareLabels(block);
		if (m_Options && m_Options->noted.count(block))
		{
			if (!IsOpen()) ContinueAfterTerminator();
			m_Body.starts.push_back({ block, m_Current, static_cast<uint32>(Current().statements.size()) });
		}
		for (const HIRNode* statement : block->stmts)
		{
			m_Frames.back().statementDepth = m_Scopes.size();
			BuildStatement(statement);
		}
		EndScope(start);
		m_Frames.pop_back();
	}

	void MirBuilder::BuildStatement(const HIRNode* s)
	{
		if (s->origin.line != 0) m_At = s->origin;
		switch (s->kind)
		{
		case HIRNode::Kind::LocalDecl: BuildDeclare(static_cast<const LocalDeclNode*>(s)); return;
		case HIRNode::Kind::Store: Evaluate(MirStatement::Kind::Eval, s); return;
		case HIRNode::Kind::ExprStmt: Evaluate(MirStatement::Kind::Eval, static_cast<const ExprStmtNode*>(s)->expr); return;
		case HIRNode::Kind::Block: BuildBlock(static_cast<const BlockNode*>(s)); return;
		case HIRNode::Kind::Defer: PushCleanup(static_cast<const DeferNode*>(s)); return;
		case HIRNode::Kind::Return: BuildReturn(static_cast<const ReturnNode*>(s)); return;
		case HIRNode::Kind::Break:
		case HIRNode::Kind::Continue:
		case HIRNode::Kind::Jump:
		{
			const uint32 id = s->kind == HIRNode::Kind::Break ? static_cast<const BreakNode*>(s)->targetLabel
				: s->kind == HIRNode::Kind::Continue ? static_cast<const ContinueNode*>(s)->targetLabel
				: static_cast<const JumpNode*>(s)->targetLabel;
			Label& label = LabelOf(id);
			if (label.block == c_MirNone) label.block = NewBlock(); // a target lowering never placed: control ends here
			BranchThroughCleanups(label.depth, label.block);
			return;
		}
		case HIRNode::Kind::Goto: BuildGoto(static_cast<const GotoNode*>(s)); return;
		case HIRNode::Kind::Label: BuildLabel(static_cast<const LabelNode*>(s)); return;
		case HIRNode::Kind::If: BuildIf(static_cast<const IfNode*>(s)); return;
		case HIRNode::Kind::While: BuildWhile(static_cast<const WhileNode*>(s)); return;
		case HIRNode::Kind::For: BuildFor(static_cast<const ForNode*>(s)); return;
		case HIRNode::Kind::Switch: BuildSwitch(static_cast<const SwitchNode*>(s)); return;
		case HIRNode::Kind::TryCatch: BuildTry(static_cast<const TryNode*>(s)); return;
		case HIRNode::Kind::Throw: BuildThrow(static_cast<const ThrowNode*>(s)); return;
		case HIRNode::Kind::Once: BuildOnce(static_cast<const OnceNode*>(s)); return;
		case HIRNode::Kind::AtExit: Evaluate(MirStatement::Kind::AtExit, s); return;
		case HIRNode::Kind::Asm:
			Evaluate(MirStatement::Kind::Asm, s);
			if (static_cast<const AsmNode*>(s)->noReturn)
			{
				End({ MirTerminator::Kind::Unreachable });
				ContinueAfterTerminator();
			}
			return;
		default: return;
		}
	}

	void MirBuilder::Evaluate(MirStatement::Kind kind, const HIRNode* node)
	{
		MirStatement statement;
		statement.kind = kind;
		statement.node = node;
		const auto [first, count] = BuildRegions(node);
		statement.firstRegion = first;
		statement.regionCount = count;
		Add(statement);
	}

	void MirBuilder::BuildDeclare(const LocalDeclNode* declaration)
	{
		Evaluate(MirStatement::Kind::Declare, declaration);
	}

	std::pair<uint32, uint32> MirBuilder::BuildRegions(const HIRNode* expression)
	{
		std::vector<const BlockNode*> blocks;
		if (expression && expression->kind == HIRNode::Kind::LocalDecl) RegionFinder(blocks).Node(static_cast<const LocalDeclNode*>(expression)->init);
		else if (expression && expression->kind == HIRNode::Kind::Asm)
		{
			RegionFinder finder(blocks);
			for (const HIRNode* input : static_cast<const AsmNode*>(expression)->inputs) finder.Node(input);
			for (const LValue* output : static_cast<const AsmNode*>(expression)->outputs) finder.Place(output);
		}
		else if (expression && expression->kind == HIRNode::Kind::AtExit) RegionFinder(blocks).Place(static_cast<const AtExitNode*>(expression)->object);
		else RegionFinder(blocks).Node(expression);
		if (blocks.empty()) return { 0, 0 };

		// The statement's block is not ended while its regions are built: the expression goes on in it after them.
		if (!IsOpen()) ContinueAfterTerminator();
		const Place outer = Save();
		std::vector<MirRegion> regions;
		for (const BlockNode* block : blocks)
		{
			const MirBlockId entry = NewBlock();
			m_Body.blocks[entry].inPlace = true;
			Enter(entry);
			BuildBlock(block);
			MirBlockId exit = c_MirNone;
			if (IsOpen())
			{
				exit = m_Current;
				End({ MirTerminator::Kind::RegionEnd });
			}
			regions.push_back({ block, entry, exit });
		}
		Restore(outer);
		const uint32 first = static_cast<uint32>(m_Body.regions.size());
		m_Body.regions.insert(m_Body.regions.end(), regions.begin(), regions.end());
		return { first, static_cast<uint32>(regions.size()) };
	}

	void MirBuilder::BuildIf(const IfNode* node)
	{
		const auto [first, count] = BuildRegions(node->cond);
		const MirBlockId thenBlock = NewBlock();
		const MirBlockId elseBlock = node->elseBlock ? NewBlock() : c_MirNone;
		const MirBlockId join = NewBlock();
		MirTerminator branch;
		branch.kind = MirTerminator::Kind::Branch;
		branch.node = node->cond;
		branch.target = thenBlock;
		branch.otherwise = elseBlock != c_MirNone ? elseBlock : join;
		branch.firstRegion = first;
		branch.regionCount = count;
		EndWith(branch);

		Enter(thenBlock);
		if (node->thenBlock) BuildBlock(node->thenBlock);
		if (IsOpen()) Goto(join);
		if (elseBlock != c_MirNone)
		{
			Enter(elseBlock);
			BuildBlock(node->elseBlock);
			if (IsOpen()) Goto(join);
		}
		Enter(join);
	}

	void MirBuilder::BuildWhile(const WhileNode* node)
	{
		const MirBlockId header = NewBlock();
		const MirBlockId body = NewBlock();
		const MirBlockId exit = NewBlock();
		DefineLabel(node->breakLabel, exit);
		DefineLabel(node->continueLabel, header);
		Goto(node->isDoWhile ? body : header);

		Enter(header);
		if (AlwaysTrue(node->cond)) Goto(body);
		else
		{
			const auto [first, count] = BuildRegions(node->cond);
			MirTerminator branch;
			branch.kind = MirTerminator::Kind::Branch;
			branch.node = node->cond;
			branch.target = body;
			branch.otherwise = exit;
			branch.firstRegion = first;
			branch.regionCount = count;
			EndWith(branch);
		}
		Enter(body);
		BuildBlock(node->body);
		if (IsOpen()) Goto(header);
		Enter(exit);
	}

	void MirBuilder::BuildFor(const ForNode* node)
	{
		// The init block is the scope of the whole loop: its cleanups run when the loop is left, not when the init
		// clause ends.
		const size_t initDepth = m_Scopes.size();
		if (!node->initBlock)
		{
			BuildForLoop(node);
			return;
		}
		m_Frames.push_back({ node->initBlock, initDepth });
		for (const HIRNode* statement : node->initBlock->stmts)
		{
			m_Frames.back().statementDepth = m_Scopes.size();
			BuildStatement(statement);
		}
		m_Frames.back().statementDepth = m_Scopes.size();
		BuildForLoop(node);
		EndScope(initDepth);
		m_Frames.pop_back();
	}

	void MirBuilder::BuildForLoop(const ForNode* node)
	{
		const MirBlockId header = NewBlock();
		const MirBlockId body = NewBlock();
		const MirBlockId step = NewBlock();
		const MirBlockId exit = NewBlock();
		DefineLabel(node->breakLabel, exit);
		DefineLabel(node->continueLabel, step);
		Goto(header);

		Enter(header);
		if (AlwaysTrue(node->cond)) Goto(body);
		else
		{
			const auto [first, count] = BuildRegions(node->cond);
			MirTerminator branch;
			branch.kind = MirTerminator::Kind::Branch;
			branch.node = node->cond;
			branch.target = body;
			branch.otherwise = exit;
			branch.firstRegion = first;
			branch.regionCount = count;
			EndWith(branch);
		}
		Enter(body);
		BuildBlock(node->body);
		if (IsOpen()) Goto(step);
		Enter(step);
		if (node->step) Evaluate(MirStatement::Kind::Eval, node->step);
		if (IsOpen()) Goto(header);
		Enter(exit);
	}

	void MirBuilder::BuildSwitch(const SwitchNode* node)
	{
		const MirBlockId exit = NewBlock();
		DefineLabel(node->breakLabel, exit);
		// Each case has its own entry label; a fallthrough Jump targets the next case's entry.
		std::vector<MirBlockId> caseBlocks(node->cases.size());
		for (size_t i = 0; i < node->cases.size(); ++i)
		{
			caseBlocks[i] = NewBlock();
			DefineLabel(node->cases[i].entryLabel, caseBlocks[i]);
		}
		const MirBlockId defaultBlock = node->defaultBlock ? NewBlock() : exit;
		if (node->defaultBlock) DefineLabel(node->defaultEntry, defaultBlock);

		const auto [first, count] = BuildRegions(node->cond);
		MirTerminator choice;
		choice.kind = MirTerminator::Kind::Switch;
		choice.node = node->cond;
		choice.otherwise = defaultBlock;
		choice.firstCase = static_cast<uint32>(m_Body.cases.size());
		choice.caseCount = static_cast<uint32>(node->cases.size());
		choice.firstRegion = first;
		choice.regionCount = count;
		for (size_t i = 0; i < node->cases.size(); ++i) m_Body.cases.push_back({ node->cases[i].value.AsInt(), caseBlocks[i] });
		EndWith(choice);

		for (size_t i = 0; i < node->cases.size(); ++i)
		{
			Enter(caseBlocks[i]);
			BuildCase(node->cases[i].block, node->cases[i].fallCheck, i + 1 < node->cases.size() ? caseBlocks[i + 1] : exit);
		}
		if (node->defaultBlock)
		{
			Enter(defaultBlock);
			BuildCase(node->defaultBlock, node->defaultFallCheck, exit);
		}
		Enter(exit);
	}

	void MirBuilder::BuildCase(const BlockNode* block, const SourceLocation& fallCheck, MirBlockId next)
	{
		const MirBlockId start = m_Current;
		BuildBlock(block);
		if (!IsOpen()) return;
		if (fallCheck.line != 0) m_Body.caseChecks.push_back({ start, m_Current, fallCheck });
		Goto(next);
	}

	void MirBuilder::BuildReturn(const ReturnNode* node)
	{
		const auto [first, count] = BuildRegions(node->value);
		if (m_Scopes.empty())
		{
			MirTerminator done;
			done.kind = MirTerminator::Kind::Return;
			done.node = node->value;
			done.firstRegion = first;
			done.regionCount = count;
			EndWith(done);
			ContinueAfterTerminator();
			return;
		}
		// The value is computed before the scopes' cleanups run (C++ order) and staged: the cleanups and a handler's
		// end come between it and the return.
		MirStatement stage;
		stage.kind = MirStatement::Kind::StageReturn;
		stage.node = node->value;
		stage.firstRegion = first;
		stage.regionCount = count;
		Add(stage);
		Goto(ReturnPathFrom(m_Scopes.size()));
		ContinueAfterTerminator();
	}

	// --- labels -----------------------------------------------------------------------------------------------

	MirBuilder::Label& MirBuilder::LabelOf(uint32 id)
	{
		if (id >= m_Labels.size()) m_Labels.resize(static_cast<size_t>(id) + 1);
		return m_Labels[id];
	}

	void MirBuilder::DeclareLabels(const BlockNode* block)
	{
		for (const HIRNode* statement : block->stmts)
			if (statement->kind == HIRNode::Kind::Label)
			{
				Label& label = LabelOf(static_cast<const LabelNode*>(statement)->labelId);
				if (label.block == c_MirNone) label.block = NewBlock();
				label.owner = block;
				label.placed = false;
			}
	}

	void MirBuilder::DefineLabel(uint32 id, MirBlockId block)
	{
		Label& label = LabelOf(id);
		label.block = block;
		label.depth = m_Scopes.size();
		label.placed = true;
		label.owner = nullptr;
	}

	size_t MirBuilder::ForwardJumpDepth(const BlockNode* owner) const
	{
		// A forward goto keeps the cleanups its label's block registered before the statement the goto is in (HIR
		// guarantees no declaration or defer lies between), and leaves everything nested inside it.
		for (size_t f = m_Frames.size(); f-- > 0; )
			if (m_Frames[f].block == owner) return m_Frames[f].statementDepth;
		return m_Scopes.size();
	}

	void MirBuilder::BuildGoto(const GotoNode* node)
	{
		Label& label = LabelOf(node->targetLabel);
		if (label.block == c_MirNone) label.block = NewBlock();
		const size_t depth = label.placed ? label.depth : ForwardJumpDepth(label.owner);
		BranchThroughCleanups(depth, label.block);
	}

	void MirBuilder::BuildLabel(const LabelNode* node)
	{
		Label& label = LabelOf(node->labelId);
		if (label.block == c_MirNone) label.block = NewBlock();
		if (IsOpen()) Goto(label.block); // falling through into the label continues there
		Enter(label.block);
		label.depth = m_Scopes.size();
		label.placed = true;
		m_Body.labels.push_back(label.block);
	}
} // namespace C4Compiler
