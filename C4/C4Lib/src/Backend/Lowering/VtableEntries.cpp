#include "Backend/Lowering/VtableEntries.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		void AppendNew(const std::vector<const FunctionRecord*>& from, std::vector<const FunctionRecord*>& to)
		{
			for (const FunctionRecord* record : from)
				if (std::find(to.begin(), to.end(), record) == to.end()) to.push_back(record);
		}
	}

	const FunctionRecord* VtableEntry(const ProgramModel& model, const ObjectType* complete, const VirtualTables::VptrLocation& location,
		const VirtualTables::Slot& slot)
	{
		const VirtualTables::Overrider overrider = VirtualTables::FinalOverrider(complete, location, slot);
		const FunctionRecord* record = model.RecordForMethod(overrider.owner, overrider.method);
		return record && (record->bodyNode || record->isLinked || record->isHostDefined) ? record : nullptr;
	}

	const std::vector<const FunctionRecord*>& VtableEntries::Of(const Type* type)
	{
		if (const auto known = m_Entries.find(type); known != m_Entries.end()) return known->second;
		std::vector<const FunctionRecord*> entries = Collect(type);
		return m_Entries[type] = std::move(entries);
	}

	std::vector<const FunctionRecord*> VtableEntries::Collect(const Type* type)
	{
		std::vector<const FunctionRecord*> entries;
		const Type* value = TypeHandle::UnwrapAliases(type);
		if (!value) return entries;
		if (value->GetKind() == TypeKind::Object) AppendObject(static_cast<const ObjectType*>(value), entries);
		else if (value->GetKind() == TypeKind::Array) AppendNew(Of(static_cast<const ArrayType*>(value)->GetElementType()), entries);
		return entries;
	}

	// As codegen fills them (VtableEmitter): a vtable per vptr location of the complete object, then the objects its
	// sub-objects' fields hold.
	void VtableEntries::AppendObject(const ObjectType* object, std::vector<const FunctionRecord*>& entries)
	{
		for (const VirtualTables::VptrLocation& location : VirtualTables::VptrLocations(object)) AppendVtable(object, location, entries);
		ClassHierarchy::ForEachSubobject(object, [&](const ObjectType* subobject, int64) {
			for (const ObjectType::FieldInfo& field : subobject->GetFields())
				if (!field.m_IsStatic && field.m_Type) AppendNew(Of(field.m_Type), entries);
		});
	}

	void VtableEntries::AppendVtable(const ObjectType* complete, const VirtualTables::VptrLocation& location, std::vector<const FunctionRecord*>& entries)
	{
		for (const VirtualTables::Slot& slot : VirtualTables::PrimarySlots(location.layout))
			if (const FunctionRecord* record = VtableEntry(m_Model, complete, location, slot))
				if (std::find(entries.begin(), entries.end(), record) == entries.end()) entries.push_back(record);
	}
} // namespace Scripting
