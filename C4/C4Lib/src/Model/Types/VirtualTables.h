#pragma once
#include "Model/Types/ObjectType.h"
#include "Model/QueryResult.h"
#include <vector>

namespace Scripting
{
	/// The virtual-dispatch model of the MSVC x64 ABI for C4's single class base + interfaces: every
	/// polymorphic sub-object has a vptr; the vtable at a vptr has the slots of the type that owns it; a slot
	/// is identified by name plus parameter types; a derived type inherits its primary base's slots in order
	/// (an override keeps the index) and appends the virtuals it introduces in declaration order.
	/// Shared by codegen (vtable contents) and HIR (which slot a call uses) so the two cannot disagree.
	namespace VirtualTables
	{
		struct Slot
		{
			const ObjectType* introducer = nullptr;            // the type whose method created the slot
			const ObjectType::MethodInfo* method = nullptr;     // that method (its name + parameter list identify the slot)
		};

		/// The slots of the vtable at `type`'s primary vptr (offset 0 of a `type` sub-object).
		std::vector<Slot> PrimarySlots(const ObjectType* type);

		/// Every vptr of an object of `type`: its offset and the type whose PrimarySlots that vtable holds.
		struct VptrLocation { int64 offset = 0; const ObjectType* layout = nullptr; };
		std::vector<VptrLocation> VptrLocations(const ObjectType* type);

		/// The method that runs for `slot` of the vtable at `location` in a complete object of `type`: the most
		/// derived declaration with the slot's signature on the path from `type` to the slot's introducer.
		struct Overrider { const ObjectType* owner = nullptr; const ObjectType::MethodInfo* method = nullptr; int64 ownerOffset = 0; };
		Overrider FinalOverrider(const ObjectType* type, const VptrLocation& location, const Slot& slot);

		/// How a call to `method` (declared in `owner`) made through a receiver of static type `receiver`
		/// dispatches: through the vptr at `vptrOffset` (relative to the receiver), slot `index`. Not virtual when
		/// the method is static or no vtable of the receiver has its signature.
		struct Dispatch { bool isVirtual = false; int64 vptrOffset = 0; int32 index = -1; };
		Dispatch DispatchFor(const ObjectType* receiver, const ObjectType* owner, const ObjectType::MethodInfo* method);

		/// Whether `method`, declared in `owner`, is virtual: declared so, an override, or an interface method.
		bool IsVirtual(const ObjectType* owner, const ObjectType::MethodInfo& method);

		bool SameSignature(const ObjectType::MethodInfo& a, const ObjectType::MethodInfo& b);

		/// The virtual methods of `type`'s bases (at any depth) that `method`, declared in `type`, overrides.
		std::vector<Overrider> Overridden(const ObjectType* type, const ObjectType::MethodInfo& method);

		/// D-29: no method overrides a final one, and only a virtual method is final. The failure carries the
		/// offending method's location.
		QueryResult<void> CheckFinalMethods(const ObjectType* type);
	}
} // namespace Scripting
