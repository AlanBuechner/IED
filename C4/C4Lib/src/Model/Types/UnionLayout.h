#pragma once
#include "Model/Types/ObjectType.h"
#include <vector>

namespace Scripting::UnionLayout
{
	struct Geometry
	{
		size_t size = 0;  // padded to the alignment
		size_t align = 1; // the strictest member's
	};

	/// Lays out the members of one union (a named union, or an anonymous union region in a class). A plain member
	/// (m_UnionViewId < 0) sits at offset 0; the members of one struct view (the same m_UnionViewId) follow each
	/// other, and every view starts at 0. Sets each member's m_UnionIntraOffset.
	Geometry LayOut(const std::vector<ObjectType::FieldInfo*>& members);

	/// The non-static members of one union: anonymous region `region` of a class, or (region -1) every member of a
	/// named union.
	std::vector<const ObjectType::FieldInfo*> RegionMembers(const Core::Vector<ObjectType::FieldInfo>& fields, int region);

	/// The members a serializer reads and writes: those of the first view (or the first plain member). The rest
	/// name the same bytes again. `members` are one union's, in declaration order.
	bool IsPrimary(const std::vector<const ObjectType::FieldInfo*>& members, const ObjectType::FieldInfo& member);
}
