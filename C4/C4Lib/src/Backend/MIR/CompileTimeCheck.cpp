#include "Backend/MIR/CompileTimeCheck.h"
#include "Backend/MIR/MirBody.h"
#include "Model/SymbolTable.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"

namespace Scripting
{
	namespace
	{
		class CompileTimeCheck
		{
		public:
			explicit CompileTimeCheck(const HIRFunction& fn) : m_Fn(fn) {}

			QueryResult<void> Run()
			{
				const MirBody& mir = *m_Fn.mir;
				std::vector<bool> seen(mir.blocks.size(), false);
				std::vector<MirBlockId> work{ MirBody::c_Entry };
				const auto reach = [&](MirBlockId id) { if (id != c_MirNone && !seen[id]) { seen[id] = true; work.push_back(id); } };
				const auto reachRegions = [&](uint32 first, uint32 count) { for (uint32 i = 0; i < count; ++i) reach(mir.regions[first + i].entry); };
				seen[MirBody::c_Entry] = true;
				while (!work.empty() && m_Result.Ok())
				{
					const MirBlock& block = mir.blocks[work.back()];
					work.pop_back();
					for (const MirStatement& statement : block.statements)
					{
						Statement(statement);
						reachRegions(statement.firstRegion, statement.regionCount);
					}
					Terminator(block.end);
					reachRegions(block.end.firstRegion, block.end.regionCount);
					mir.ForEachSuccessor(block, reach);
				}
				return m_Result;
			}

		private:
			void Fail(const Core::String& what)
			{
				if (m_Result.Ok())
					m_Result = QueryResult<void>::Fail(QueryError::NotAValue, "'" + CompileTimeName(*m_Fn.record) + "' is [compile_time], but " + what, m_At);
			}

			void Statement(const MirStatement& statement)
			{
				m_At = statement.at;
				switch (statement.kind)
				{
				case MirStatement::Kind::Eval:
				case MirStatement::Kind::StageReturn: Node(statement.node); return;
				case MirStatement::Kind::Declare: Node(static_cast<const LocalDeclNode*>(statement.node)->init); return;
				case MirStatement::Kind::Asm: return Fail("it runs inline assembly");
				case MirStatement::Kind::AtExit: return Fail("it registers a destruction at exit");
				case MirStatement::Kind::OnceEnd: return Fail("it has a static local");
				case MirStatement::Kind::SetFlag: return;
				}
			}

			void Terminator(const MirTerminator& end)
			{
				m_At = end.at;
				switch (end.kind)
				{
				case MirTerminator::Kind::Branch:
				case MirTerminator::Kind::Switch:
				case MirTerminator::Kind::Return: Node(end.node); return;
				case MirTerminator::Kind::OnceBegin: return Fail("it has a static local");
				default: return; // a throw ends the evaluation with an error of its own
				}
			}

			template <typename Nodes> void All(const Nodes& nodes) { for (const HIRNode* node : nodes) Node(node); }

			void Node(const HIRNode* node)
			{
				if (!node || !m_Result.Ok()) return;
				switch (node->kind)
				{
				case HIRNode::Kind::Load: return Place(static_cast<const LoadNode*>(node)->loc);
				case HIRNode::Kind::Store: Place(static_cast<const StoreNode*>(node)->target); return Node(static_cast<const StoreNode*>(node)->value);
				case HIRNode::Kind::Call: return Call(*static_cast<const CallNode*>(node));
				case HIRNode::Kind::IntrinsicOp: Node(static_cast<const IntrinsicOpNode*>(node)->lhs); return Node(static_cast<const IntrinsicOpNode*>(node)->rhs);
				case HIRNode::Kind::Convert: return Node(static_cast<const ConvertNode*>(node)->src);
				case HIRNode::Kind::Construct: return Construct(*static_cast<const ConstructNode*>(node));
				case HIRNode::Kind::AddressOf: return Place(static_cast<const AddressOfNode*>(node)->loc);
				case HIRNode::Kind::AddressArithmetic:
					Node(static_cast<const AddressArithmeticNode*>(node)->base);
					return Node(static_cast<const AddressArithmeticNode*>(node)->offset);
				case HIRNode::Kind::Ternary:
				{
					const auto* choice = static_cast<const TernaryNode*>(node);
					Node(choice->cond);
					Node(choice->thenExpr);
					return Node(choice->elseExpr);
				}
				case HIRNode::Kind::Sequence:
					for (const HIRNode* step : static_cast<const SequenceNode*>(node)->steps)
						if (step->kind != HIRNode::Kind::Block) Node(step); // a block step is a region: its blocks are walked
					return;
				case HIRNode::Kind::Transfer: return Node(static_cast<const TransferNode*>(node)->copy);
				case HIRNode::Kind::Constant: return;
				case HIRNode::Kind::New: return Fail("it allocates on the heap ('new')");
				case HIRNode::Kind::Delete: return Fail("it frees memory ('delete')");
				case HIRNode::Kind::Atomic: return Fail("it uses an atomic operation");
				case HIRNode::Kind::Va: return Fail("it reads variadic arguments");
				case HIRNode::Kind::Simd: case HIRNode::Kind::VectorBuild: return Fail("it uses SIMD vectors");
				case HIRNode::Kind::DynamicCast: return Fail("it checks a type at run time");
				case HIRNode::Kind::MakeFunc: case HIRNode::Kind::FuncCode: case HIRNode::Kind::FunctionAddress:
					return Fail("it takes a function's address");
				case HIRNode::Kind::Relocate: return Fail("it relocates an object");
				case HIRNode::Kind::InitList: return Fail("it builds an initializer list");
				case HIRNode::Kind::GcType: return Fail("it takes a type's GC record ('gc_type_of')");
				default: return Fail("it does what compile time cannot run");
				}
			}

			void Construct(const ConstructNode& construct)
			{
				if (construct.ctor && !MayRunAtCompileTime(*construct.ctor))
					return Fail("it builds a value with '" + CompileTimeName(*construct.ctor) + "', which is not [compile_time]");
				All(construct.args);
			}

			void Call(const CallNode& call)
			{
				if (call.virtualIndex >= 0) return Fail("it calls a virtual method");
				if (!call.callee || call.target || call.code) return Fail("it calls through a pointer or a func value");
				if (!MayRunAtCompileTime(*call.callee)) return Fail("it calls '" + CompileTimeName(*call.callee) + "', which is not [compile_time]");
				Place(call.receiver);
				All(call.args);
			}

			void Place(const LValue* place)
			{
				for (; place && m_Result.Ok(); )
					switch (place->kind)
					{
					case LValue::Kind::Field: case LValue::Kind::Base: place = place->base; break;
					case LValue::Kind::Deref: return Node(place->addr);
					case LValue::Kind::Slot:
						if (m_Fn.slots[place->slot].cls == SlotClass::Static) return Fail("it has a static local");
						return;
					case LValue::Kind::Global:
						return Fail("it uses the global variable '" + (place->sym ? place->sym->plainName : Core::String("?")) + "', which is not a constant");
					case LValue::Kind::Lane: case LValue::Kind::Swizzle: case LValue::Kind::VectorLaneDyn: return Fail("it uses a packed vector's lanes");
					case LValue::Kind::IndexOp: return Fail("it uses an indexer that returns by value");
					case LValue::Kind::This: return;
					}
			}

			const HIRFunction& m_Fn;
			SourceLocation m_At;
			QueryResult<void> m_Result = QueryResult<void>::Success();
		};
	}

	bool MayRunAtCompileTime(const FunctionRecord& record)
	{
		return record.synthesized != LifecycleOp::None || IsCompileTime(record);
	}

	Core::String CompileTimeName(const FunctionRecord& record)
	{
		return record.name.empty() ? record.mangledName : record.name;
	}

	QueryResult<void> CheckCompileTimeBody(const HIRFunction& fn)
	{
		const AnnotationItem* mark = fn.record ? CompileTimeMark(*fn.record) : nullptr;
		if (!mark || !fn.mir) return QueryResult<void>::Success();
		if (!mark->GetArguments().Empty())
			return QueryResult<void>::Fail(QueryError::NotAValue, "[compile_time] takes no arguments", mark->startLocation);
		return CompileTimeCheck(fn).Run();
	}
} // namespace Scripting
