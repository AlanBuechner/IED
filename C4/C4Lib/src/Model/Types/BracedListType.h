#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// The type of a braced argument (`Take({ 5, 6 })`) while a call is resolved: it has no type of its own, only
	/// a number of entries, which decides the parameters it can bind (D-57): an `__init_list<T>`, an object those
	/// entries build, an array of at least as many elements. It is never a value's type.
	class BracedListType : public Type
	{
	public:
		static constexpr TypeKind kTypeKind = TypeKind::BracedList;
		explicit BracedListType(uint32 entries) : Type(TypeKind::BracedList), m_Entries(entries) {}
		Core::String GetName() const override { return "{" + std::to_string(m_Entries) + " entries}"; }

		uint32 GetEntryCount() const { return m_Entries; }

	private:
		uint32 m_Entries = 0;
	};
} // namespace Scripting
