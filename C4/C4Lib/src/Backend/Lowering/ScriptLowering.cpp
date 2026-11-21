#include "Backend/Lowering/ScriptLowering.h"
#include "Model/Types/QualifiedName.h"
#include "Script/ScriptModulePasses.h"

namespace Scripting
{
	bool ScriptLowering::HostHas(const std::string& symbol) const
	{
		return hostSymbols && hostSymbols->Has(symbol);
	}

	bool ScriptLowering::IsHostStorage(const SymbolInfo& variable) const
	{
		const std::string symbol = variable.mangledName.c_str();
		return IsHostUnit(variable.definingUnit) || HostHas(symbol) || HostHas(ThreadLocalAccessor(symbol));
	}

	bool ScriptLowering::HostRegisters(const ObjectType& type) const
	{
		return hostTypes && hostTypes->Has(QualifiedName(&type).c_str());
	}
} // namespace Scripting
