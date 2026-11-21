#include "Backend/Lowering/BodyNeeds.h"
#include "Backend/HIR/CountedArray.h"
#include "Backend/HIR/HIRWalk.h"
#include "Model/Types/NativeMemory.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		template <typename T> void AddOnce(std::vector<T>& list, const T& item)
		{
			if (std::none_of(list.begin(), list.end(), [&item](const T& known) { return known == item; })) list.push_back(item);
		}

		// What emitting one node references (CallEmitter, ObjectEmitter, FuncEmitter, EhTables, StaticStorage and the
		// vptr stores of ExprEmitter).
		class NeedsCollector
		{
		public:
			explicit NeedsCollector(BodyNeeds& needs) : m_Needs(needs) {}

			void Node(const HIRNode& node)
			{
				switch (node.kind)
				{
				case HIRNode::Kind::Call: Call(static_cast<const CallNode&>(node)); return;
				case HIRNode::Kind::FunctionAddress: Function(static_cast<const FunctionAddressNode&>(node).fn); return;
				case HIRNode::Kind::MakeFunc: MakeFunc(static_cast<const MakeFuncNode&>(node)); return;
				case HIRNode::Kind::New: New(static_cast<const NewNode&>(node)); return;
				case HIRNode::Kind::Construct:
					Function(static_cast<const ConstructNode&>(node).ctor);
					Object(static_cast<const ConstructNode&>(node).objectType);
					return;
				case HIRNode::Kind::Relocate: Object(static_cast<const RelocateNode&>(node).objectType); return;
				case HIRNode::Kind::Throw: Throw(static_cast<const ThrowNode&>(node)); return;
				case HIRNode::Kind::AtExit: Function(static_cast<const AtExitNode&>(node).destroy); return;
				case HIRNode::Kind::Store: Store(static_cast<const StoreNode&>(node)); return;
				case HIRNode::Kind::Load: Load(static_cast<const LoadNode&>(node)); return;
				default: return;
				}
			}

			// Every local and temporary starts zeroed with its vptrs; a static local starts as their image unless it
			// starts holding a constant (IRContext).
			void Slot(const HIRFunction::Slot& slot)
			{
				const bool zeroed = slot.cls == SlotClass::Local || slot.cls == SlotClass::Temp || (slot.cls == SlotClass::Static && !slot.initial);
				if (zeroed) Object(slot.type);
			}

		private:
			void Function(const FunctionRecord* record)
			{
				if (record) AddOnce(m_Needs.functions, record);
			}

			void Object(const Type* type)
			{
				if (type) AddOnce(m_Needs.objects, type);
			}

			void Census(const Type* type, uint64 countBytes)
			{
				if (type && NativeMemory::Holds(type)) AddOnce(m_Needs.census, CensusNeed{ type, countBytes });
			}

			// A call through a given address names its callee only for its signature.
			void Call(const CallNode& call)
			{
				if (!call.code) Function(call.callee);
			}

			void MakeFunc(const MakeFuncNode& make)
			{
				if (make.from == MakeFuncNode::From::PointerClosure) Function(make.code);
				if (make.from != MakeFuncNode::From::HeapClosure) return;
				Function(make.code);
				Function(make.finalizer);
				if (make.source) Census(make.source->type, 0);
			}

			// The storage (a heap allocation counts its census), the vptrs, the create, and the finalizer a heap object or
			// counted array registers; a counted array keeps its element count before its elements.
			void New(const NewNode& made)
			{
				Function(made.ctor);
				Function(made.finalizer);
				Object(made.objectType);
				if (made.placement || !made.objectType) return;
				if (!made.count) Census(made.objectType, 0);
				else if (made.finalizer) Census(made.objectType, CountedArray::PrefixBytes(made.objectType->GetAlignment()));
			}

			void Throw(const ThrowNode& thrown)
			{
				if (!thrown.value) return; // a rethrow raises the exception object it has
				Function(thrown.destroy);
				for (const auto& [type, copy] : thrown.copies) Function(copy);
			}

			// A store keeps or sets the vptrs of the object it writes; a lane or a run of a vector holds none.
			void Store(const StoreNode& store)
			{
				const LValue::Kind kind = store.target ? store.target->kind : LValue::Kind::Lane;
				if (kind != LValue::Kind::Lane && kind != LValue::Kind::Swizzle && kind != LValue::Kind::VectorLaneDyn) Object(store.target->type);
			}

			// An object read from storage that may be a sub-object of a larger one takes its own type's vptrs.
			void Load(const LoadNode& load)
			{
				const LValue::Kind kind = load.loc ? load.loc->kind : LValue::Kind::Slot;
				if (kind == LValue::Kind::Deref || kind == LValue::Kind::This || kind == LValue::Kind::Base) Object(load.type);
			}

			BodyNeeds& m_Needs;
		};

		void Collect(const HIRFunction& body, BodyNeeds& needs)
		{
			NeedsCollector collector(needs);
			WalkEmitted(body, { [&collector](const HIRNode& node) { collector.Node(node); }, nullptr });
			for (const HIRFunction::Slot& slot : body.slots) collector.Slot(slot);
			for (const std::unique_ptr<HIRFunction>& nested : body.nestedFunctions) Collect(*nested, needs);
		}
	}

	BodyNeeds NeedsOf(const HIRFunction& body)
	{
		BodyNeeds needs;
		Collect(body, needs);
		return needs;
	}
} // namespace Scripting
