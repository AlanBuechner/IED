#include "Model/Types/PrimitiveType.h"

namespace Scripting
{
	PrimitiveType::PrimitiveType(TypeKind kind)
		: Type(kind)
	{
		switch (kind) {
		case TypeKind::Void:
			m_Size = 0;
			m_Alignment = 1;
			break;
		case TypeKind::Bool:
			m_Size = 1;
			m_Alignment = 1;
			break;
		case TypeKind::Int8:
		case TypeKind::UInt8:
		case TypeKind::SNorm8:
		case TypeKind::UNorm8:
			m_Size = 1;
			m_Alignment = 1;
			break;
		case TypeKind::Int16:
		case TypeKind::UInt16:
		case TypeKind::SNorm16:
		case TypeKind::UNorm16:
			m_Size = 2;
			m_Alignment = 2;
			break;
		case TypeKind::Int32:
		case TypeKind::UInt32:
		case TypeKind::SNorm32:
		case TypeKind::UNorm32:
		case TypeKind::Float32:
			m_Size = 4;
			m_Alignment = 4;
			break;
		case TypeKind::Int64:
		case TypeKind::UInt64:
		case TypeKind::SNorm64:
		case TypeKind::UNorm64:
		case TypeKind::Float64:
			m_Size = 8;
			m_Alignment = 8;
			break;
		case TypeKind::Char8:
			m_Size = 1;
			m_Alignment = 1;
			break;
		case TypeKind::Char16:
			m_Size = 2;
			m_Alignment = 2;
			break;
		case TypeKind::Char32:
			m_Size = 4;
			m_Alignment = 4;
			break;
		default:
			m_Size = 0;
			m_Alignment = 1;
			break;
		}
	}

	Core::String PrimitiveType::GetName() const
	{
		switch (m_Kind) {
		case TypeKind::Void: return "void";
		case TypeKind::Bool: return "bool";
		case TypeKind::Int8: return "int8";
		case TypeKind::Int16: return "int16";
		case TypeKind::Int32: return "int32";
		case TypeKind::Int64: return "int64";
		case TypeKind::UInt8: return "uint8";
		case TypeKind::UInt16: return "uint16";
		case TypeKind::UInt32: return "uint32";
		case TypeKind::UInt64: return "uint64";
		case TypeKind::SNorm8: return "snorm8";
		case TypeKind::SNorm16: return "snorm16";
		case TypeKind::SNorm32: return "snorm32";
		case TypeKind::SNorm64: return "snorm64";
		case TypeKind::UNorm8: return "unorm8";
		case TypeKind::UNorm16: return "unorm16";
		case TypeKind::UNorm32: return "unorm32";
		case TypeKind::UNorm64: return "unorm64";
		case TypeKind::Float32: return "float";
		case TypeKind::Float64: return "double";
		case TypeKind::Char8: return "char8";
		case TypeKind::Char16: return "char16";
		case TypeKind::Char32: return "char32";
		default: return "<unknown>";
		}
	}
} // namespace Scripting
