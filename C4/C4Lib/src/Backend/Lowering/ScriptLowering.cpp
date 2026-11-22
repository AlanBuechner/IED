#include "Backend/Lowering/ScriptLowering.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/AstNode.h"
#include "Script/ScriptModulePasses.h"

namespace C4Compiler
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

	bool ScriptLowering::HostDeclares(const ObjectType& type) const
	{
		const AstNode* declaration = type.GetDeclNode();
		return declaration && type.GetTemplateArguments().Empty() && IsHostUnit(declaration->startLocation.sourceId);
	}
} // namespace C4Compiler
