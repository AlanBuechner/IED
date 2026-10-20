#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// Binding a reference (docs/language/04 4.4): a reference local, parameter or field names a location of its
	/// referent's type, held as its address. A reference field (docs/language/05) is that address stored in the
	/// object: it is bound where the object starts - a braced entry, its initializer, or an assignment in its
	/// object's own create - and every other use reads and writes the referent. A reference owns nothing.
	namespace ReferenceBinding
	{
		/// The reference type `t` is (through aliases), or null.
		const ReferenceType* Of(const Type* t);
		/// Why `ref` cannot name a location of type `located`, or "".
		Core::String Problem(const ReferenceType* ref, const Type* located);
		/// The address `ref` binds to from `init`, as a `pointer`; `name` is what the messages call the reference.
		QueryResult<const HIRNode*> Address(LoweringContext& ctx, const ReferenceType* ref, const Value& init, const Type* pointer,
			const Core::String& name);
		/// The pointer type a binding of `ref` stores.
		const Type* PointerFor(LoweringContext& ctx, const ReferenceType* ref);

		/// What a use of the reference field `field` names: its referent, which remembers the field.
		const LValue* Referent(LoweringContext& ctx, const LValue* field);
		/// Whether an assignment to `target` binds the reference field behind it: `target` is the referent of a
		/// field of the object a create is building.
		bool BindsInCreate(const LoweringContext& ctx, const LValue* target);
		/// `field = address`, the store binding a reference field.
		const HIRNode* Bind(LoweringContext& ctx, const LValue* field, const HIRNode* address);
		/// The first reference field of `type` (not its bases) with no initializer that `bound` does not hold, or null.
		const ObjectType::FieldInfo* FirstUnbound(const ObjectType* type, const std::vector<const ObjectType::FieldInfo*>& bound);
		/// The message for an object of `type` starting with `field` unbound.
		Core::String Unbound(const ObjectType* type, const ObjectType::FieldInfo& field);
	}
} // namespace Scripting
