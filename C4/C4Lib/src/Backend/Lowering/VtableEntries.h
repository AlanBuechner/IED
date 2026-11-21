#pragma once
#include "Model/SymbolTable.h"
#include "Model/Types/VirtualTables.h"
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class ProgramModel;

	/// The function the vtable of a complete `complete` object at `location` runs for `slot`: its final overrider when
	/// that has code (a body, a foreign definition or the host's); null for a slot nothing implements (c4_purecall).
	const FunctionRecord* VtableEntry(const ProgramModel& model, const ObjectType* complete, const VirtualTables::VptrLocation& location,
		const VirtualTables::Slot& slot);

	/// The functions the vtables of an object of a type point at: through its own vptrs, its bases' and those of its
	/// fields and array elements - what holding such an object makes the program need (D-16: a virtual member of a
	/// class-template instance is compiled with the instance's vtable). A reference holds no object (BUG-438). Each
	/// type's are worked out once, in slot order, each function once.
	class VtableEntries
	{
	public:
		explicit VtableEntries(const ProgramModel& model) : m_Model(model) {}

		const std::vector<const FunctionRecord*>& Of(const Type* type);

	private:
		std::vector<const FunctionRecord*> Collect(const Type* type);
		void AppendObject(const ObjectType* object, std::vector<const FunctionRecord*>& entries);
		void AppendVtable(const ObjectType* complete, const VirtualTables::VptrLocation& location, std::vector<const FunctionRecord*>& entries);

		const ProgramModel& m_Model;
		std::unordered_map<const Type*, std::vector<const FunctionRecord*>> m_Entries;
	};
} // namespace Scripting
