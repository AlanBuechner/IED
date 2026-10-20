#include "Model/Types/Type.h"
#include "Model/Types/EnumType.h"

namespace Scripting
{
	Core::String ObjectSizeProblem(const Type& type)
	{
		if (type.GetSize() <= kLargestObjectBytes) return "";
		return "'" + type.GetName() + "' is " + std::to_string(type.GetSize()) + " bytes, more than the largest object (" +
			std::to_string(kLargestObjectBytes) + " bytes): allocate it in parts or hold it in a 'Core::Array'";
	}

	// --- Type helpers (numeric-kind classification + C conversion rules) ---

	bool Type::IsSNormKind(TypeKind k)
	{
		switch (k) {
		case TypeKind::SNorm8: case TypeKind::SNorm16: case TypeKind::SNorm32: case TypeKind::SNorm64:
			return true;
		default: return false;
		}
	}

	bool Type::IsUNormKind(TypeKind k)
	{
		switch (k) {
		case TypeKind::UNorm8: case TypeKind::UNorm16: case TypeKind::UNorm32: case TypeKind::UNorm64:
			return true;
		default: return false;
		}
	}

	bool Type::IsPrimitiveKind(TypeKind k)
	{
		switch (k) {
		case TypeKind::Void:
		case TypeKind::Bool:
		case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
		case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
		case TypeKind::SNorm8: case TypeKind::SNorm16: case TypeKind::SNorm32: case TypeKind::SNorm64:
		case TypeKind::UNorm8: case TypeKind::UNorm16: case TypeKind::UNorm32: case TypeKind::UNorm64:
		case TypeKind::Float32: case TypeKind::Float64:
		case TypeKind::Char8: case TypeKind::Char16: case TypeKind::Char32:
			return true;
		default:
			return false;
		}
	}

	bool Type::IsIntegral() const
	{
		switch (m_Kind) {
		case TypeKind::Bool:
		case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
		case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
		case TypeKind::SNorm8: case TypeKind::SNorm16: case TypeKind::SNorm32: case TypeKind::SNorm64:
		case TypeKind::UNorm8: case TypeKind::UNorm16: case TypeKind::UNorm32: case TypeKind::UNorm64:
		case TypeKind::Char8: case TypeKind::Char16: case TypeKind::Char32:
		case TypeKind::Enum:
			return true;
		default:
			return false;
		}
	}

	bool Type::IsFloatingPoint() const
	{
		return m_Kind == TypeKind::Float32 || m_Kind == TypeKind::Float64;
	}

	bool Type::IsNumeric() const
	{
		return IsIntegral() || IsFloatingPoint();
	}

	namespace
	{
		// Bit width of an integral kind (normalized types are stored as their intN/uintN), 0 if not integral.
		uint32_t IntegralKindBits(TypeKind k)
		{
			switch (k)
			{
			case TypeKind::Bool:
			case TypeKind::Int8: case TypeKind::UInt8:
			case TypeKind::SNorm8: case TypeKind::UNorm8:
			case TypeKind::Char8: return 8;
			case TypeKind::Int16: case TypeKind::UInt16: case TypeKind::Char16:
			case TypeKind::SNorm16: case TypeKind::UNorm16: return 16;
			case TypeKind::Int32: case TypeKind::UInt32: case TypeKind::Char32:
			case TypeKind::SNorm32: case TypeKind::UNorm32: return 32;
			case TypeKind::Int64: case TypeKind::UInt64:
			case TypeKind::SNorm64: case TypeKind::UNorm64: return 64;
			default: return 0;
			}
		}

		bool KindIsUnsignedInteger(TypeKind k)
		{
			switch (k)
			{
			case TypeKind::UInt8: case TypeKind::UInt16:
			case TypeKind::UInt32: case TypeKind::UInt64:
			case TypeKind::UNorm8: case TypeKind::UNorm16:
			case TypeKind::UNorm32: case TypeKind::UNorm64:
			case TypeKind::Char16: case TypeKind::Char32: return true; // code units are unsigned
			default: return false;
			}
		}

		TypeKind IntKindOf(uint32_t bits, bool isUnsigned)
		{
			switch (bits)
			{
			case 8:  return isUnsigned ? TypeKind::UInt8 : TypeKind::Int8;
			case 16: return isUnsigned ? TypeKind::UInt16 : TypeKind::Int16;
			case 32: return isUnsigned ? TypeKind::UInt32 : TypeKind::Int32;
			default: return isUnsigned ? TypeKind::UInt64 : TypeKind::Int64;
			}
		}
	}

	TypeKind Type::IntegerPromotionKind(TypeKind k)
	{
		const uint32_t bits = IntegralKindBits(k);
		if (bits == 0) return TypeKind::Invalid; // not integral: no promotion applies
		if (bits < 32) return TypeKind::Int32;   // narrower than int promotes to SIGNED int32
		return k;                                  // int32 and wider keep their kind
	}

	TypeKind Type::UsualArithmeticResultKind(TypeKind a, TypeKind b)
	{
		// D-38: a normalized operand is its value, a fraction: the operation runs in float (double for a
		// 32- or 64-bit normalized type or a double operand) and gives that type.
		if (IsNormalizedKind(a) || IsNormalizedKind(b))
		{
			auto wide = [](TypeKind k) {
				return k == TypeKind::Float64 || (IsNormalizedKind(k) && IntegralKindBits(k) >= 32);
			};
			return wide(a) || wide(b) ? TypeKind::Float64 : TypeKind::Float32;
		}
		const bool aFloat = (a == TypeKind::Float32 || a == TypeKind::Float64);
		const bool bFloat = (b == TypeKind::Float32 || b == TypeKind::Float64);

		if (aFloat || bFloat)
		{
			if (aFloat && bFloat)
				return (a == TypeKind::Float64 || b == TypeKind::Float64) ? TypeKind::Float64 : TypeKind::Float32;
			return aFloat ? a : b; // the floating operand wins over an integer one
		}

		const uint32_t aBits = IntegralKindBits(a);
		const uint32_t bBits = IntegralKindBits(b);
		if (aBits == 0 || bBits == 0) return TypeKind::Invalid; // not both integral

		// Integer promotions: narrower than int32 promotes to SIGNED int32.
		uint32_t lBits = aBits, rBits = bBits;
		bool lUnsigned = KindIsUnsignedInteger(a), rUnsigned = KindIsUnsignedInteger(b);
		if (lBits < 32) { lBits = 32; lUnsigned = false; }
		if (rBits < 32) { rBits = 32; rUnsigned = false; }

		bool commonUnsigned;
		if (lBits > rBits) { lBits = lBits; commonUnsigned = lUnsigned; }
		else if (rBits > lBits) { lBits = rBits; commonUnsigned = rUnsigned; }
		else { commonUnsigned = lUnsigned || rUnsigned; }
		return IntKindOf(lBits, commonUnsigned);
	}

	bool Type::IsUnsignedInteger() const
	{
		switch (m_Kind)
		{
		case TypeKind::UInt8: case TypeKind::UInt16:
		case TypeKind::UInt32: case TypeKind::UInt64:
		case TypeKind::UNorm8: case TypeKind::UNorm16:
		case TypeKind::UNorm32: case TypeKind::UNorm64:
		case TypeKind::Char16: case TypeKind::Char32: // code units are unsigned (C++ char16_t/char32_t)
			return true;
		case TypeKind::Enum: // an enum is its underlying integer
		{
			const Type* underlying = static_cast<const EnumType*>(this)->GetUnderlyingType();
			return underlying && underlying->IsUnsignedInteger();
		}
		default:
			return false;
		}
	}
} // namespace Scripting
