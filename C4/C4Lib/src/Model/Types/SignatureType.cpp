#include "Model/Types/SignatureType.h"
#include "Model/Types/AliasType.h"

namespace Scripting
{
	SignatureType::SignatureType(TypeKind kind, const char* former, const Type* returnType, const Core::Vector<const Type*>& paramTypes)
		: Type(kind), m_Former(former), m_ReturnType(returnType), m_ParameterTypes(paramTypes)
	{
	}

	Core::String SignatureType::ToString() const
	{
		Core::String result = Core::String(m_Former) + "<";
		result += m_ReturnType ? m_ReturnType->ToString() : Core::String("void");
		result += "(";
		for (uint32 i = 0; i < m_ParameterTypes.Count(); i++)
		{
			if (i > 0) result += ", ";
			if (m_ParameterTypes[i]) result += m_ParameterTypes[i]->ToString();
		}
		result += ")>";
		return result;
	}

	namespace
	{
		// Aliases are transparent: `alias Index = uint32;` names the same type.
		const Type* WithoutAliases(const Type* t)
		{
			while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
			return t;
		}
	}

	bool SignatureType::HasSignature(const Type* ret, const Core::Vector<const Type*>& params) const
	{
		if (WithoutAliases(ret) != WithoutAliases(m_ReturnType) || params.Count() != m_ParameterTypes.Count()) return false;
		for (uint32 i = 0; i < params.Count(); ++i)
			if (WithoutAliases(params[i]) != WithoutAliases(m_ParameterTypes[i])) return false;
		return true;
	}
} // namespace Scripting
