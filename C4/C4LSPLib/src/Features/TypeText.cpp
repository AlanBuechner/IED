#include "Features/TypeText.h"
#include "Model/Types.h"
#include "Model/TypeHandle.h"

namespace C4LSP::Features
{
	Core::String TypeText(const Scripting::Type* type)
	{
		if (!type)
			return "<unknown>";
		return type->ToString();
	}

	const Scripting::Type* NamedType(const Scripting::Type* type)
	{
		while (type)
		{
			type = Scripting::TypeHandle::UnwrapToValue(type);
			if (const auto* pointer = dynamic_cast<const Scripting::PointerType*>(type))
				type = pointer->GetPointeeType();
			else if (const auto* array = dynamic_cast<const Scripting::ArrayType*>(type))
				type = array->GetElementType();
			else
				return type;
		}
		return nullptr;
	}

	const Scripting::AstNode* DeclarationOfNamedType(const Scripting::Type* type)
	{
		const Scripting::Type* named = NamedType(type);
		if (const auto* object = dynamic_cast<const Scripting::ObjectType*>(named))
			return object->GetDeclNode();
		if (const auto* enumType = dynamic_cast<const Scripting::EnumType*>(named))
			return enumType->GetDeclNode();
		if (const auto* unionType = dynamic_cast<const Scripting::UnionType*>(named))
			return unionType->GetDeclNode();
		return nullptr;
	}

	Core::String FunctionSignature(const Scripting::FunctionType* func)
	{
		if (!func)
			return "";

		Core::String text = TypeText(func->GetReturnType()) + " (";
		const auto& params = func->GetParameters();
		for (uint32 i = 0; i < params.Count(); ++i)
		{
			if (i > 0)
				text += ", ";
			text += TypeText(params[i].m_Type);
			if (!params[i].mName.empty())
				text += " " + params[i].mName;
		}
		if (func->IsVariadic() && params.Count() > 0)
			text += ", ...";
		text += ")";
		return text;
	}
} // namespace C4LSP::Features
