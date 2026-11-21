#include "Backend/HIR/HIRWalk.h"

namespace Scripting
{
	namespace
	{
		// Follows each node's and lvalue's operands as codegen emits them (ExprEmitter, RegionEmitter, ObjectEmitter).
		class EmittedWalk
		{
		public:
			explicit EmittedWalk(const HIRVisitor& visit) : m_Visit(visit) {}

			void Node(const HIRNode* node)
			{
				if (!node) return;
				if (m_Visit.node) m_Visit.node(*node);
				Operands(*node);
			}

			void Place(const LValue* lvalue)
			{
				if (!lvalue) return;
				if (m_Visit.lvalue) m_Visit.lvalue(*lvalue);
				switch (lvalue->kind)
				{
				case LValue::Kind::Field:
				case LValue::Kind::Base:
				case LValue::Kind::Lane:
				case LValue::Kind::Swizzle: Place(lvalue->base); break;
				case LValue::Kind::VectorLaneDyn: Place(lvalue->base); Node(lvalue->index); break;
				case LValue::Kind::Deref: Node(lvalue->addr); break;
				case LValue::Kind::IndexOp: Node(lvalue->read); break; // the getter call, built with its receiver and index
				case LValue::Kind::Slot:
				case LValue::Kind::Global:
				case LValue::Kind::This: break;
				}
			}

		private:
			template <typename Nodes> void Each(const Nodes& nodes)
			{
				for (const HIRNode* node : nodes) Node(node);
			}

			void Operands(const HIRNode& node);
			void Statements(const HIRNode& node);

			const HIRVisitor& m_Visit;
		};

		template <typename T> const T& As(const HIRNode& node) { return static_cast<const T&>(node); }

		void EmittedWalk::Operands(const HIRNode& node)
		{
			switch (node.kind)
			{
			case HIRNode::Kind::Constant:
			case HIRNode::Kind::GcType:
			case HIRNode::Kind::FunctionAddress: return;
			case HIRNode::Kind::Load: Place(As<LoadNode>(node).loc); return;
			case HIRNode::Kind::Call:
			{
				const CallNode& call = As<CallNode>(node);
				Place(call.receiver);
				Node(call.target);
				Node(call.code);
				Each(call.args);
				return;
			}
			case HIRNode::Kind::IntrinsicOp: Node(As<IntrinsicOpNode>(node).lhs); Node(As<IntrinsicOpNode>(node).rhs); return;
			case HIRNode::Kind::Convert: Node(As<ConvertNode>(node).src); return;
			case HIRNode::Kind::New:
			{
				const NewNode& made = As<NewNode>(node);
				Node(made.placement);
				Node(made.count);
				Each(made.args);
				Place(made.elements);
				Node(made.initialize);
				return;
			}
			case HIRNode::Kind::Delete: Node(As<DeleteNode>(node).operand); return;
			case HIRNode::Kind::Construct: Each(As<ConstructNode>(node).args); return;
			case HIRNode::Kind::AddressOf: Place(As<AddressOfNode>(node).loc); return;
			case HIRNode::Kind::AddressArithmetic: Node(As<AddressArithmeticNode>(node).base); Node(As<AddressArithmeticNode>(node).offset); return;
			case HIRNode::Kind::Ternary:
			{
				const TernaryNode& ternary = As<TernaryNode>(node);
				Node(ternary.cond);
				Node(ternary.thenExpr);
				Node(ternary.elseExpr);
				return;
			}
			case HIRNode::Kind::InitList: Each(As<InitListNode>(node).elements); return;
			case HIRNode::Kind::VectorBuild: Each(As<VectorBuildNode>(node).elems); return;
			case HIRNode::Kind::Sequence: Each(As<SequenceNode>(node).steps); return;
			case HIRNode::Kind::DynamicCast: Node(As<DynamicCastNode>(node).operand); return;
			case HIRNode::Kind::Relocate: Node(As<RelocateNode>(node).destination); Node(As<RelocateNode>(node).source); return;
			case HIRNode::Kind::MakeFunc: Node(As<MakeFuncNode>(node).source); return;
			case HIRNode::Kind::FuncCode: Node(As<FuncCodeNode>(node).func); return;
			case HIRNode::Kind::Transfer: Node(As<TransferNode>(node).copy); return; // the copy, or the move LastUseMoves put there
			case HIRNode::Kind::Va: Node(As<VaNode>(node).list); Node(As<VaNode>(node).source); return;
			case HIRNode::Kind::Atomic:
			{
				const AtomicNode& atomic = As<AtomicNode>(node);
				Node(atomic.orderValue);
				Node(atomic.failureOrderValue);
				Node(atomic.address);
				Node(atomic.value);
				Node(atomic.expected);
				return;
			}
			case HIRNode::Kind::Simd: Each(As<SimdNode>(node).operands); return;
			case HIRNode::Kind::Store: Place(As<StoreNode>(node).target); Node(As<StoreNode>(node).value); return;
			default: Statements(node); return;
			}
		}

		void EmittedWalk::Statements(const HIRNode& node)
		{
			switch (node.kind)
			{
			case HIRNode::Kind::Block: Each(As<BlockNode>(node).stmts); return;
			case HIRNode::Kind::If: Node(As<IfNode>(node).cond); Node(As<IfNode>(node).thenBlock); Node(As<IfNode>(node).elseBlock); return;
			case HIRNode::Kind::While: Node(As<WhileNode>(node).cond); Node(As<WhileNode>(node).body); return;
			case HIRNode::Kind::For:
			{
				const ForNode& loop = As<ForNode>(node);
				Node(loop.initBlock);
				Node(loop.cond);
				Node(loop.step);
				Node(loop.body);
				return;
			}
			case HIRNode::Kind::Switch:
			{
				const SwitchNode& choice = As<SwitchNode>(node);
				Node(choice.cond);
				for (const SwitchCase& entry : choice.cases) Node(entry.block);
				Node(choice.defaultBlock);
				return;
			}
			case HIRNode::Kind::Return: Node(As<ReturnNode>(node).value); return;
			case HIRNode::Kind::ExprStmt: Node(As<ExprStmtNode>(node).expr); return;
			case HIRNode::Kind::LocalDecl: Node(As<LocalDeclNode>(node).init); return;
			case HIRNode::Kind::TryCatch:
			{
				const TryNode& attempt = As<TryNode>(node);
				Node(attempt.tryBlock);
				for (const TryNode::CatchClause& clause : attempt.catches) Node(clause.block);
				return;
			}
			case HIRNode::Kind::Throw: Node(As<ThrowNode>(node).value); return;
			case HIRNode::Kind::Defer: Node(As<DeferNode>(node).body); return;
			case HIRNode::Kind::Once: Node(As<OnceNode>(node).body); return;
			case HIRNode::Kind::AtExit: Place(As<AtExitNode>(node).object); return;
			case HIRNode::Kind::Asm:
			{
				const AsmNode& block = As<AsmNode>(node);
				for (const LValue* output : block.outputs) Place(output);
				Each(block.inputs);
				return;
			}
			default: return; // Break, Continue, Jump, Goto, Label: no operands
			}
		}
	}

	void WalkEmitted(const HIRFunction& function, const HIRVisitor& visit)
	{
		EmittedWalk(visit).Node(function.root);
	}
} // namespace Scripting
