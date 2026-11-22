#include "Features/TypeText.h"
#include "Model/Types.h"
#include "Model/TypeHandle.h"

namespace C4LSP::Features
{
	Core::String TypeText(const C4Compiler::Type* type)
	{
		if (!type)
			return "<unknown>";
		return type->ToString();
	}

	const C4Compiler::Type* NamedType(const C4Compiler::Type* type)
	{
		while (type)
		{
			type = C4Compiler::TypeHandle::UnwrapToValue(type);
			if (const auto* pointer = dynamic_cast<const C4Compiler::PointerType*>(type))
				type = pointer->GetPointeeType();
			else if (const auto* array = dynamic_cast<const C4Compiler::ArrayType*>(type))
				type = array->GetElementType();
			else
				return type;
		}
		return nullptr;
	}

	const C4Compiler::AstNode* DeclarationOfNamedType(const C4Compiler::Type* type)
	{
		const C4Compiler::Type* named = NamedType(type);
		if (const auto* object = dynamic_cast<const C4Compiler::ObjectType*>(named))
			return object->GetDeclNode();
		if (const auto* enumType = dynamic_cast<const C4Compiler::EnumType*>(named))
			return enumType->GetDeclNode();
		if (const auto* unionType = dynamic_cast<const C4Compiler::UnionType*>(named))
			return unionType->GetDeclNode();
		return nullptr;
	}

	Core::String FunctionSignature(const C4Compiler::FunctionType* func)
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
