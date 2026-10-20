#pragma once
#include "Model/Types/ObjectType.h"
#include "Model/QueryResult.h"
#include <functional>
#include <vector>

namespace Scripting
{
	/// Facts about an object type's base sub-objects, read from its MSVC-compatible layout: where each base
	/// sits, whether a base is reachable along one path or several, and where a member found by name lives.
	/// Every hierarchy walk in the compiler goes through these functions, so every base counts - not only the
	/// first one.
	namespace ClassHierarchy
	{
		struct BaseLocation
		{
			bool found = false;
			bool ambiguous = false; // `target` occurs as more than one sub-object
			int64 offset = 0;       // of the first occurrence
		};

		/// Where `target` sits inside an object of `type` (offset 0 and found for the type itself).
		BaseLocation LocateBase(const ObjectType* type, const ObjectType* target);
		bool DerivesFrom(const ObjectType* type, const ObjectType* target);
		/// `derived` may name `base` as a base: a class, struct or interface that is neither `derived` nor derives
		/// from it.
		QueryResult<void> CheckCanInherit(const ObjectType* derived, const Type* base);

		/// The byte delta that turns a pointer to `from` into a pointer to `to` for related object types: the
		/// base's offset for an upcast, its negation for a downcast, 0 when unrelated or identical.
		int64 PointerDelta(const ObjectType* from, const ObjectType* to);

		/// Every sub-object of `type` - the type itself first, then its bases depth-first in declaration order -
		/// with its offset inside `type`.
		void ForEachSubobject(const ObjectType* type, const std::function<void(const ObjectType*, int64)>& visit);

		/// The same walk as a list (MSVC's base class array order): each entry knows how many of the entries
		/// after it are its own bases, and whether its type occurs as more than one sub-object.
		struct Subobject
		{
			const ObjectType* type = nullptr;
			int64 offset = 0;
			uint32 containedBases = 0;
			bool ambiguous = false;
		};
		std::vector<Subobject> Subobjects(const ObjectType* type);

		/// A field found by name lookup from `type`: own fields first, then the bases. `owner` declares it and
		/// sits at `ownerOffset`; the field's own offset is relative to `owner`. Ambiguous when two different
		/// sub-objects provide an instance field of that name.
		struct FieldHit
		{
			const ObjectType::FieldInfo* field = nullptr;
			const ObjectType* owner = nullptr;
			int64 ownerOffset = 0;
			bool ambiguous = false;
		};
		FieldHit FindField(const ObjectType* type, const Core::String& name);

		/// The base-list rules: no base is final; at most one class base, and it is written first; an interface
		/// derives only from interfaces; an interface has no instance fields.
		QueryResult<void> CheckBaseList(const ObjectType* type);
		QueryResult<void> CheckInterfaceMembers(const ObjectType* type);
	}
} // namespace Scripting
