#pragma once
#include "Compiler/AskedNames.h"
#include "Model/SymbolTable.h"
#include <string>

namespace Scripting
{
	class ObjectType;

	/// What lowering a script needs of its host (docs/runtime/04 section 4.2): what the host's table and reflection hold,
	/// and which units are the host's interface files.
	struct ScriptLowering
	{
		const AskedNames* hostSymbols = nullptr; // the names of the host table's entries
		const AskedNames* hostTypes = nullptr;   // the types the host's reflection registered
		uint32 interfaceUnits = 0;               // units 1..interfaceUnits are the host's files

		/// The host's table names `symbol`.
		bool HostHas(const std::string& symbol) const;
		/// `unit` is one of the host's interface files.
		bool IsHostUnit(uint32 unit) const { return unit >= 1 && unit <= interfaceUnits; }
		/// Storage the host has: declared in an interface file, or named by the host table (itself or its thread_local's
		/// accessor). Its start ran in the host.
		bool IsHostStorage(const SymbolInfo& variable) const;
		/// The host's reflection registered `type`: the script's image leaves it out.
		bool HostRegisters(const ObjectType& type) const;
	};
} // namespace Scripting
