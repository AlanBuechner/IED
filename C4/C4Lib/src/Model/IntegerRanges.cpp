#include "Model/IntegerRanges.h"

namespace Scripting
{
	namespace
	{
		constexpr uint64 kBitsPerByte = 8;
		constexpr uint64 kWidestInteger = 64;

		uint64 Bits(const Type* t) { return static_cast<uint64>(t->GetSize()) * kBitsPerByte; }
	}

	bool IntegerRanges::IsUnsigned(const Type* integer) { return integer->GetKind() == TypeKind::Bool || integer->IsUnsignedInteger(); }

	uint64 IntegerRanges::MaxOf(const Type* integer)
	{
		if (integer->GetKind() == TypeKind::Bool) return 1;
		if (IsUnsigned(integer)) return Bits(integer) >= kWidestInteger ? ~0ull : (1ull << Bits(integer)) - 1;
		return (1ull << (Bits(integer) - 1)) - 1;
	}

	bool IntegerRanges::FitsNegative(int64 value, const Type* integer)
	{
		if (IsUnsigned(integer)) return false;
		return Bits(integer) >= kWidestInteger || value >= -static_cast<int64>(1ull << (Bits(integer) - 1));
	}

	bool IntegerRanges::Fits(int64 value, const Type* integer)
	{
		if (Bits(integer) >= kWidestInteger && integer->GetKind() != TypeKind::Bool) return true;
		return value < 0 ? FitsNegative(value, integer) : static_cast<uint64>(value) <= MaxOf(integer);
	}
} // namespace Scripting
