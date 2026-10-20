#pragma once
#include "Backend/HIR/HIRValue.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	/// The memory classes of strict aliasing (D-64): an access touches only memory of its own class, except Bytes,
	/// which may touch any. None is what has no memory at all (an empty object); it takes the class it meets.
	enum class AliasClass : uint8 { None, Bytes, Bool, Int16, Int32, Int64, Float32, Float64, Pointer, Count };

	/// The class of two parts of one access: theirs when they agree, else Bytes.
	AliasClass Meet(AliasClass a, AliasClass b);

	/// The class of each type and each access of a program; a type's is worked out once.
	class AliasClassifier
	{
	public:
		/// `hasVptrs` says whether an object holds vptrs (pointers the compiler writes).
		explicit AliasClassifier(std::function<bool(const Type*)> hasVptrs) : m_HasVptrs(std::move(hasVptrs)) {}

		/// A value of `type`: its parts' class when they share one, else Bytes.
		AliasClass Of(const Type* type);
		/// An access of `accessed` at `loc`: the type's class, or Bytes when the access goes through a union member
		/// and the union's members are not all of that class.
		AliasClass OfAccess(const LValue* loc, const Type* accessed);

	private:
		AliasClass PartsOf(const Type* type);
		AliasClass Compute(const Type* type);
		AliasClass OfObject(const ObjectType* object);
		AliasClass OfMembers(const std::vector<const ObjectType::FieldInfo*>& members);
		AliasClass UnionsAlong(const LValue* loc);
		AliasClass UnionHolding(const LValue* field);

		std::function<bool(const Type*)> m_HasVptrs;
		std::unordered_map<const Type*, AliasClass> m_Parts;
	};
} // namespace Scripting
