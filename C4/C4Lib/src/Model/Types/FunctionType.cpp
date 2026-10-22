#include "Model/Types/FunctionType.h"
#include "Model/Types/QualifiedName.h"
#include "Core/Core.h"

namespace Scripting
{
	FunctionType::FunctionType(const Type* returnType, const Core::Vector<ParameterInfo>& params, bool variadic)
		: Type(TypeKind::Function), m_ReturnType(returnType), m_Parameters(params), m_IsVariadic(variadic)
	{
		// Functions have no size in the type system - they're addresses
		m_Size = sizeof(void*);
		m_Alignment = alignof(void*);
	}

	Core::String FunctionType::GetName() const
	{
		return "<function>";
	}

	Core::String FunctionType::ToString() const
	{
		Core::String result;
		if (m_ReturnType) {
			result += m_ReturnType->ToString();
		}
		else {
			result += "void";
		}
		result += " (";
		for (uint32 i = 0; i < m_Parameters.Count(); i++) {
			if (i > 0) result += ", ";
			const auto& param = m_Parameters[i];
			if (param.m_Type) {
				result += param.m_Type->ToString();
			}
			else {
				result += "<unknown>";
			}
			if (!param.mName.empty()) {
				result += " " + param.mName;
			}
		}
		if (m_IsVariadic) {
			if (m_Parameters.Count() > 0) result += ", ";
			result += "...";
		}
		result += ")";
		return result;
	}
	Core::String FunctionType::OverloadSuffix() const
	{
		Core::String s = std::to_string(m_Parameters.Count()); // arity first: () vs (int32) differ even if names collide
		for (const auto& p : m_Parameters)
			s += p.m_Type ? SymbolToken(p.m_Type) : Core::String("unk");
		if (m_IsVariadic) s += "v";
		return s;
	}
} // namespace Scripting
