#pragma once
#include "Model/Types/ObjectType.h"
#include <vector>

namespace Scripting::AggregateSlots
{
	/// One position braces fill (`{a, b, c}`): a field, or one lane of a packed region.
	struct Slot
	{
		Core::String name;                            // what a keyed entry names it by; empty for an unnamed lane
		const Type* type = nullptr;                   // what the entry converts to
		size_t offset = 0;                            // from the object's start
		const ObjectType::FieldInfo* field = nullptr; // the field, or the packed region's storage for a lane
		int32 lane = -1;                              // the lane, or -1 for a whole field
	};

	/// The positions braces fill, in order: each instance field outside a union, the fields of each union's first
	/// view (the other views name the same bytes again, as in C), and the lanes of each packed region's first view
	/// (BUG-195..197). Statics have their own storage and are never a slot.
	std::vector<Slot> Of(const ObjectType& type);

	/// The slot a keyed entry (`{ .y = 2 }` or `{ y: 2 }`) names: any instance field, any view of a union, any lane
	/// name; false when nothing has that name.
	bool Named(const ObjectType& type, const Core::String& name, Slot& out);
}
