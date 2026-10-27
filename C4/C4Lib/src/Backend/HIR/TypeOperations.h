#pragma once
#include "Backend/HIR/HIR.h"
#include "Compiler/ErrorTracker.h"
#include <string>

namespace Scripting
{
	class ProgramModel;
	struct FunctionRecord;

	/// What reflection runs on a type's storage for code that knows the type only at run time (docs/runtime/02
	/// section 2): `new (p) T()`, `destroy(p)` and `new (to) T(*from)`.
	enum class TypeOperation : uint8 { Construct, Destroy, Copy };

	/// What reflection records of a type: its flags (`reflect_TYPE_*`, C4Runtime/ReflectABI.h) and which of its
	/// operations run code, each through a thunk. An operation that runs nothing (zero is the start, a byte copy is the
	/// copy) has no thunk.
	struct TypeOperations
	{
		uint32 flags = 0;
		bool construct = false;
		bool destroy = false;
		bool copy = false;
	};

	/// The flags and thunks of an object type, a union or an enum. A type whose objects cannot exist on their own (an
	/// abstract class, an interface) has neither a start nor a copy; one whose creates all take arguments has no
	/// start; one that cannot be copied (section 6.4), or holds a template instance whose copy needs an argument that
	/// cannot be, has no copy.
	TypeOperations TypeOperationsOf(ProgramModel& model, const Type* type);

	/// The flags of a union or an enum, which hold no lifecycle: bytes start, copy and end one.
	uint32 PlainTypeFlags(const Type* type);

	/// The symbol of `type`'s thunk for `op`: `__c4_reflect_construct_`, `__c4_reflect_destroy_` or `__c4_reflect_copy_` and
	/// its escaped qualified name (apart from the compiler-written `__c4_copy_` of the type, which it may call).
	std::string TypeOperationSymbol(const Type* type, TypeOperation op);

	/// `void(void* object)`, or `void(void* to, const void* from)` for a copy.
	const FunctionType* TypeOperationThunkType(ProgramModel& model, TypeOperation op);

	/// The body of `type`'s thunk for `op`, whose own record is `thunk`: `new ((T*)object) T()` (zero storage, its vptrs,
	/// then its start), `destroy((T*)object)`, or `new ((T*)to) T(*(const T*)from)`. Incomplete (with the reason in
	/// `errors`) when the operation does not lower.
	HIRFunction LowerTypeOperationThunk(ProgramModel& model, ErrorTracker& errors, const ObjectType& type, TypeOperation op, const FunctionRecord& thunk);
} // namespace Scripting
