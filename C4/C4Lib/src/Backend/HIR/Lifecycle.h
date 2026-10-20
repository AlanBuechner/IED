#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/LifecycleFacts.h"
#include <functional>

namespace Scripting
{
	/// How objects come into existence, are copied and go away (docs/language/06, D-03, D-05..D-07), as
	/// HIR. Construction runs the base, then the fields, then the chosen create; destruction runs the
	/// delete body, then the fields in reverse, then the base; a copy without a copy operator is memberwise.
	/// The functions that do it are the type's own lifecycle methods or compiler-written ones
	/// (`ProgramModel::EnsureSynthesizedLifecycle`), and every call to them is direct (never virtual).
	class Lifecycle
	{
	public:
		explicit Lifecycle(LoweringContext& ctx) : m_Ctx(ctx) {}

		/// The function for each step of `type`; null when zeroed storage / plain bytes already do it.
		const FunctionRecord* DefaultCreate(const ObjectType* type);
		/// The arguments a call of the default create passes: the defaults of its parameters (none for a create
		/// with no parameters, or a synthesized one).
		std::vector<const HIRNode*> DefaultArguments(const FunctionRecord* create);
		const FunctionRecord* Destroy(const ObjectType* type);
		const FunctionRecord* Copy(const ObjectType* type);

		/// Statements appended to `region`. `object` is complete storage of its lvalue type.
		void Construct(BlockNode* region, const LValue* object);                  // zeroed -> default-constructed
		/// The elements of `array` in [begin, end), default-constructed (nothing when they need nothing).
		void ConstructElements(BlockNode* region, const LValue* array, uint64 begin, uint64 end);
		/// Element `source` of `array` copied into each zeroed element in [begin, end).
		void CopyElement(BlockNode* region, const LValue* array, uint64 source, uint64 begin, uint64 end);
		void DestroyNow(BlockNode* region, const LValue* object);
		/// Destroying a single object as an expression; null when there is nothing to run.
		const HIRNode* DestroyCall(const LValue* object);
		DeferNode* ScheduleDestroy(BlockNode* region, const LValue* object);      // a scope-exit cleanup (null when none is needed)
		void DestroyAtExit(BlockNode* region, const LValue* object);              // static storage: at program exit
		void CopyInto(BlockNode* region, const LValue* target, const LValue* source); // zeroed target

		/// Values. CopyOf builds a fresh copy of `source` in a temporary and yields it (the caller owns it);
		/// Take yields the value of `source` and leaves `source` zero (relocation out of an lvalue).
		QueryResult<const HIRNode*> CopyOf(const LValue* source);
		const HIRNode* Take(const LValue* source);
		/// A fresh default-constructed `type` (an array's elements each constructed).
		const HIRNode* DefaultValue(const Type* type);
		/// A fresh `type` that `build` makes in zeroed temporary storage (its statements may be loops); the
		/// consumer owns it. `build` guards its own steps when they can throw part-way.
		QueryResult<const HIRNode*> BuildTemporary(const Type* type, const char* name,
			const std::function<QueryResult<void>(BlockNode* steps, const LValue* temp)>& build);

		/// True when assigning a `t` is more than storing bytes: it declares `=`, or it (or a part) needs
		/// destruction or a copy.
		bool AssignmentNeedsLifecycle(const Type* t);

		/// `target = source` for such a type (D-06): the type's `=` when declared; otherwise the new value is
		/// made first (a copy of an lvalue, or the fresh object itself), then the old value is destroyed and
		/// the new one relocated in - safe when both name the same object. The result names the target.
		QueryResult<const LValue*> Assign(const LValue* target, const Value& source);

		/// A new object the consumer owns: a by-value call result, `T(...)`, a copy, `take(x)`, an object
		/// `?:`. Anything else of object type is a view of an existing object (a load).
		static bool IsFresh(const HIRNode* node);
		/// Whether a copy of `source` may become a move at its last use: a local's or by-value parameter's slot.
		bool MayMove(const LValue* source) const;

		/// A statement-owned temporary holding `fresh`: an lvalue the statement can take the address of
		/// (a receiver, a member access, a `const T&` argument). It is destroyed when its statement ends.
		const LValue* Temporary(const HIRNode* fresh);

		/// `value`, then the temporaries created since `mark` destroyed and zeroed: the end of a full
		/// expression inside a statement (a condition, a loop step). They stay with their statement, so an
		/// exception later in it finds them zero.
		const HIRNode* EndFullExpression(const HIRNode* value, size_t mark);
		/// The same for a declaration that is not a statement of its own (a for-loop init): the statements
		/// destroying and zeroing those temporaries, appended to `region`.
		void EndTemporaries(BlockNode* region, size_t mark);

		/// The statements `lower` appends to `block` as one statement: the temporaries its expressions create
		/// are destroyed when it ends, apart from those of any statement it is part of.
		QueryResult<void> LowerStatement(BlockNode* block, const std::function<QueryResult<void>()>& lower);
		/// The statements of `block` from `mark` on move into a block that owns the pending temporaries.
		void PlaceTemporaries(BlockNode* block, size_t mark);

		/// `v` evaluated for its side effects only: an object is not loaded whole, and a fresh object that
		/// needs destruction is owned by a statement temporary.
		const HIRNode* Discarded(const Value& v);

		/// The location a member access or method call works on when `value` is not one already: a by-value
		/// call result that needs no destruction is materialized by codegen; any other object value becomes a
		/// statement temporary. Both are ephemeral (a store into them would be lost). Null for a non-object.
		const LValue* AddressableObject(const Value& value, const Type* type);

		/// The body of a compiler-written lifecycle function, into `root` (`this` is the object).
		QueryResult<void> LowerSynthesized(const FunctionRecord* rec, BlockNode* root);

		/// The parts of a user create / delete / copy the compiler adds around its body: the base (unless the
		/// body's first statement handles it) and the fields.
		QueryResult<void> ConstructParts(BlockNode* root, const ObjectType* type, bool baseHandledByBody, const AstNode* site);
		/// A field with an initializer (`int32 x = 7;`) starts from it: lowered in the create, stored into the field.
		QueryResult<void> InitializeField(BlockNode* root, const LValue* field, const ObjectType::FieldInfo& info);
		QueryResult<void> ConstructField(BlockNode* root, const LValue* field, const ObjectType::FieldInfo& info, const AstNode* site);
		void ScheduleDestroyParts(BlockNode* root, const ObjectType* type);
		void CopyBase(BlockNode* root, const ObjectType* type, const LValue* other);
		/// A thread_local global, static member or static local: each thread has its own copy.
		bool HasThreadStorage(const LValue* object) const;

		/// The block a create or copy of `type` builds `this` in (or a fill builds `object` in). When the
		/// parts need destruction it is guarded: an exception leaving it destroys the parts (the ones not
		/// built yet are still zero) and continues unwinding, as C++ destroys a throwing constructor's
		/// finished subobjects.
		BlockNode* PartsGuard(BlockNode* root, const ObjectType* type);
		/// The same for an initializer list filling `object` in place: an object's members and base, or an
		/// array's elements, are destroyed if the fill throws.
		BlockNode* FillGuard(BlockNode* root, const LValue* object);

		/// `value` evaluated now into a hidden slot (appended to `prelude`), read back later by the returned
		/// node: a value that needs destruction is a statement temporary until it is moved out, so an
		/// exception between the two destroys it.
		const HIRNode* Hold(const HIRNode* value, std::vector<const HIRNode*>& prelude);

	private:
		static const ObjectType* AsObject(const Type* t);
		static const ArrayType* AsArray(const Type* t);
		const LValue* This(const ObjectType* type);
		const LValue* FieldOf(const LValue* object, const ObjectType::FieldInfo& field);
		const LValue* BaseOf(const LValue* object, const ObjectType* type, const ObjectType* base);
		const HIRNode* DirectCall(const FunctionRecord* rec, const LValue* receiver, std::vector<const HIRNode*> args);
		void ForEachIndex(BlockNode* region, uint64 begin, uint64 end, bool reverse,
			const std::function<void(BlockNode*, const HIRNode*)>& visit);
		const HIRNode* IndexConstant(uint64 index);
		const LValue* ElementOf(const LValue* array, const HIRNode* index);
		const FunctionRecord* Synthesized(const ObjectType* type, LifecycleOp op);
		void DestroyTemporaries(std::vector<const HIRNode*>& steps, size_t mark);
		void DestroyPartsNow(BlockNode* region, const LValue* object, const ObjectType* type); // fields reversed, then the base
		bool PartsNeedDestroy(const ObjectType* type);
		BlockNode* GuardWith(BlockNode* root, const std::function<void(BlockNode*)>& undo);

		LoweringContext& m_Ctx;
	};
} // namespace Scripting
