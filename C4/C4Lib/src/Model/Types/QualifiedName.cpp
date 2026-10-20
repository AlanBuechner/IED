#include "Model/Types/QualifiedName.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/UnionType.h"
#include <cctype>
#include <charconv>

namespace Scripting
{
	namespace
	{
		Core::String Scoped(const Core::Vector<Core::String>& enclosing, const Core::String& name)
		{
			Core::String out;
			for (const Core::String& part : enclosing) out += part + "::";
			return out + name;
		}

		Core::String ObjectName(const ObjectType& object)
		{
			Core::String name = Scoped(object.GetEnclosingNames(), object.GetName());
			if (object.GetTemplateArguments().Empty()) return name;
			return name + "<" + TemplateArgumentsText(object) + ">";
		}
	}

	Core::String QualifiedName(const Type* type)
	{
		if (!type) return "void";
		switch (type->GetKind())
		{
		case TypeKind::Alias:
			return QualifiedName(static_cast<const AliasType*>(type)->GetAliasedType());
		case TypeKind::Pointer:
		{
			const auto* pointer = static_cast<const PointerType*>(type);
			return (pointer->IsConstReferent() ? "const " : "") + QualifiedName(pointer->GetPointeeType()) + "*";
		}
		case TypeKind::Reference:
		{
			const auto* reference = static_cast<const ReferenceType*>(type);
			return (reference->IsConstReferent() ? "const " : "") + QualifiedName(reference->GetReferentType()) + "&";
		}
		case TypeKind::Array:
		{
			const auto* array = static_cast<const ArrayType*>(type);
			return QualifiedName(array->GetElementType()) + "[" + std::to_string(array->GetElementCount()) + "]";
		}
		case TypeKind::Object:
			return ObjectName(static_cast<const ObjectType&>(*type));
		case TypeKind::Enum:
		{
			const auto& en = static_cast<const EnumType&>(*type);
			return Scoped(en.GetEnclosingNames(), en.GetName());
		}
		case TypeKind::Union:
		{
			const auto& un = static_cast<const UnionType&>(*type);
			return Scoped(un.GetEnclosingNames(), un.GetName());
		}
		default:
			return type->GetName();
		}
	}

	Core::String TemplateArgumentsText(const ObjectType& instance)
	{
		Core::String text;
		for (const ObjectType::TemplateArgument& argument : instance.GetTemplateArguments())
		{
			if (!text.empty()) text += ", ";
			text += argument.type ? QualifiedName(argument.type) : ConstantText(argument.value);
		}
		return text;
	}

	Core::String EscapedText(const Core::String& text)
	{
		static const char kHex[] = "0123456789abcdef";
		Core::String out;
		for (char c : text)
		{
			const unsigned char byte = static_cast<unsigned char>(c);
			if (std::isalnum(byte)) { out += c; continue; }
			out += '_';
			out += kHex[byte >> 4];
			out += kHex[byte & 15];
		}
		return out;
	}

	Core::String ConstantText(const ConstantValue& value)
	{
		switch (value.kind)
		{
		case ConstantValue::Kind::Bool: return value.booleanValue ? "true" : "false";
		case ConstantValue::Kind::Float64:
		{
			char text[32];
			const auto written = std::to_chars(text, text + sizeof(text), value.floatValue);
			Core::String out(text, written.ptr);
			if (out.find_first_of(".ein") == Core::String::npos) out += ".0"; // a float never reads as an integer
			return out;
		}
		default: return std::to_string(value.AsInt());
		}
	}

	Core::String SymbolToken(const Type* type) { return SymbolTokenOf(QualifiedName(type)); }

	Core::String SymbolTokenOf(const Core::String& text)
	{
		Core::String out;
		for (char c : text)
		{
			switch (c)
			{
			case '*': out += 'P'; break;
			case '&': out += 'R'; break;
			case '[': out += 'A'; break;
			case ']': break;
			case '<': out += 'T'; break;
			case '>': out += 'E'; break;
			default:  out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_'; break;
			}
		}
		return out;
	}
} // namespace Scripting
