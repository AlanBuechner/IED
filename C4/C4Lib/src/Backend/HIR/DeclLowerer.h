#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/Construction.h"
#include "Backend/HIR/PackElements.h"
#include "Model/Types/AggregateSlots.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h"
#include <memory>

namespace Scripting
{
	struct VariableDefinitionNode; struct Declarator; class ReferenceType;

	/// A value local declared from an already lowered value (a destructured element, D-34).
	struct LocalStart
	{
		const AstNode* site = nullptr;
		Core::String name;
		const Type* type = nullptr;
		bool isConst = false;
	};

	/// What a declaration starts its variable from: `= value`, `= {...}`, `(args)`, or nothing.
	struct VariableStart
	{
		const AstNode* initializer = nullptr;                   // an expression or a braced list
		const Core::Vector<Ref<AstNode>>* arguments = nullptr;  // `T v(args);`
		const AstNode* site = nullptr;
		Core::String name;
	};

	/// Variable declarations: one slot per declarator of a local, initializer coercion, reference locals,
	/// direct construction `T v(args)`, braced aggregate initialization and default construction - and the end
	/// of each variable: a value local's at its scope exit; a static local's, a global's and a static member's
	/// at program exit, after a start that runs once (docs/language/06).
	class DeclLowerer
	{
	public:
		explicit DeclLowerer(LoweringContext& ctx) : m_Ctx(ctx) {}

		QueryResult<void> LowerLocalDecl(const VariableDefinitionNode* n, BlockNode* region);
		/// A global or a static member, in the program's global initialization: its start, then its
		/// destruction registered for exit.
		QueryResult<void> StartGlobal(const VariableStart& start, const LValue* global, BlockNode* region);
		/// The start of an object with static storage that starts when control first reaches it (a static local, a
		/// script's thread_local), as one statement, then its destruction at exit. One that throws part-way leaves the
		/// storage zero again.
		QueryResult<void> StartStatic(const VariableStart& start, const LValue* storage, BlockNode* body);
		/// A braced list as a value of `type` outside a declaration (`T{ y = 4 }`, an array field of `T{...}`):
		/// filled in a temporary exactly as a declaration of `type` would be.
		QueryResult<const HIRNode*> BracedValue(const BracedInitializerNode* br, const Type* type, const AstNode* site);
		/// The local `local` names, starting from `value`, in scope from here to the end of `region`'s scope.
		QueryResult<void> DeclareValue(const LocalStart& local, const Value& value, BlockNode* region);
		/// A braced start of `target`: an `__init_list<T>`, a create taking the elements, or a fill.
		QueryResult<void> StartFromBraces(const BracedInitializerNode* br, const LValue* target, BlockNode* region, const AstNode* site, const Core::String& name);
		/// How many leading elements of an array a braced list starts: one past the last element an entry names.
		QueryResult<uint64> ElementsNamed(const BracedInitializerNode* br, const Core::String& name);

	private:
		QueryResult<void> LowerLocal(const VariableDefinitionNode* n, const Declarator& d, const Type* t, BlockNode* region);
		/// A static local: the slot is program storage, started inside a Once region the first time control
		/// reaches the declaration. A start that only stores a constant becomes the storage's initial value.
		QueryResult<void> LowerStaticLocal(const VariableDefinitionNode* n, const Declarator& d, const Type* t, BlockNode* region);
		QueryResult<void> StartStaticReference(const VariableDefinitionNode* n, const Declarator& d, const ReferenceType* ref,
			const LValue* storage, BlockNode* body);
		QueryResult<void> StartObject(const VariableStart& start, const LValue* target, BlockNode* body);
		QueryResult<void> StoreInitialValue(const VariableStart& start, const LValue* target, BlockNode* body);
		bool StartsAsConstant(const BlockNode* body, uint32 slot);
		const ConstantNode* StoredConstant(const HIRNode* value, const Type* type);

		/// `V v = {1, 2};` / `int32[4] a = {...}` for a local: declare the slot, then fill it in place.
		QueryResult<void> LowerAggregateInit(const VariableDefinitionNode* n, const Type* t, const BracedInitializerNode* br, BlockNode* region);
		/// Fill an aggregate lvalue from a braced initializer; a nested brace recurses.
		QueryResult<void> FillAggregate(const Type* t, const BracedInitializerNode* br,
			const LValue* baseLoc, BlockNode* region, const Core::String& name);
		QueryResult<void> StoreEntry(const LValue& target, const AstNode* valueNode, BlockNode* block, const Core::String& name);
		/// One entry of a braced list as it fills a part: a pack expansion's copies are an item each.
		struct BracedItem
		{
			const BracedInitializerNode::Entry* entry = nullptr;
			std::shared_ptr<const PackElements> copies;
			uint32 copy = 0;
			const AstNode* Value() const { return copies ? copies->Pattern() : entry->m_Value.Get(); }
		};
		QueryResult<std::vector<BracedItem>> BracedItems(const BracedInitializerNode* br);
		QueryResult<void> StoreItem(const LValue& target, const BracedItem& item, BlockNode* block, const Core::String& name);
		QueryResult<void> FillLanes(const SwizzleType* swz, const BracedInitializerNode* br, const LValue* baseLoc,
			BlockNode* block, const Core::String& name);
		QueryResult<void> FillFields(const ObjectType* obj, const BracedInitializerNode* br, const LValue* baseLoc,
			BlockNode* block, const Core::String& name);
		/// The elements one entry of an array's braced list starts (inclusive), and its value.
		struct ElementEntry { uint64 first = 0; uint64 last = 0; BracedItem item; };
		QueryResult<std::vector<ElementEntry>> ElementEntries(uint64 count, const BracedInitializerNode* br, const Core::String& name);
		QueryResult<void> FillElements(const ArrayType* ar, const BracedInitializerNode* br, const LValue* baseLoc,
			BlockNode* block, const Core::String& name);
		// A slot of `object` as a location: the field, or the lane of its packed storage.
		const LValue* SlotLValue(const AggregateSlots::Slot& slot, const LValue* object);
		QueryResult<void> FillOmittedFields(const ObjectType* obj, const std::vector<const ObjectType::FieldInfo*>& given,
			const LValue* baseLoc, BlockNode* block);

		/// The construction (unless initialized from a value) and the scope-exit destruction of a value local.
		QueryResult<void> LowerValueLocalLifetime(const VariableDefinitionNode* n, const Type* t, uint32 slot,
			bool hasInitializer, const std::vector<ConstructionArgument>& ctorArgs, BlockNode* region);
		QueryResult<void> ConstructFrom(const ObjectType* obj, const std::vector<ConstructionArgument>& args, bool braced,
			const LValue* self, BlockNode* region, const AstNode* site);
		/// Run a construction plan on the zeroed object `self`: its create, or its fields in order.
		QueryResult<void> RunPlan(const ConstructionPlan& plan, const LValue* self, BlockNode* region);

		/// What a reference local's slot holds: the address of the location `init` names.
		QueryResult<void> BindSlot(const AstNode* site, const Core::String& name, const Type* type, uint32 slot, const ReferenceType* ref,
			bool isConst);
		QueryResult<void> BindName(const VariableDefinitionNode* n, const Core::String& name, const Type* type,
			uint32 slot, const ReferenceType* ref);
		const LValue* SlotLValue(uint32 slot, const Type* type);

		LoweringContext& m_Ctx;
	};
} // namespace Scripting
