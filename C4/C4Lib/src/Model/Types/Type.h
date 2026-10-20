#pragma once
#include "Core/Core.h"
#include "Model/Types/TypeKind.h"

namespace Scripting
{
	/// No object is larger: its size must fit a signed 32-bit offset, as in MSVC (C2148).
	constexpr uint64 kLargestObjectBytes = 0x7FFFFFFF;

	/// `'int32[999999999]' is 3999999996 bytes, ...` when `type` is larger than any object may be, else empty.
	Core::String ObjectSizeProblem(const class Type& type);

	/// The abstract type object every concrete C4 type derives from. Deliberately thin (doc NOTE):
	/// it carries only what EVERY kind shares - the kind tag, layout (size/alignment), and a name -
	/// plus the numeric-kind predicates that operate on the kind alone. Shape-specific behavior lives
	/// on the derived types; indirection/wrapper reasoning lives on TypeHandle (the definitive wrapper).
	class Type
	{
	public:
		explicit Type(TypeKind kind) : m_Kind(kind) {}
		virtual ~Type() = default;

		TypeKind GetKind() const { return m_Kind; }
		bool IsValid() const { return m_Kind != TypeKind::Invalid; }

		// Numeric-kind classification (operates on the kind tag, shared by every type).
		static bool IsPrimitiveKind(TypeKind k);
		bool IsPrimitive() const { return IsPrimitiveKind(m_Kind); }
		bool IsIntegral() const;
		bool IsFloatingPoint() const;
		bool IsNumeric() const;

		/// C usual arithmetic conversions for two numeric operand kinds: the common result kind of a
		/// binary arithmetic/compare expression. Integer promotions (narrower-than-int32 -> signed int32),
		/// then the wider width wins, and at equal width unsigned wins. A float/double pair yields the
		/// wider; an int + float pair yields the float. Non-numeric operands return Invalid (no conversion).
		static TypeKind UsualArithmeticResultKind(TypeKind a, TypeKind b);

		/// C integer promotion for a UNARY operand kind: anything integral narrower than int32 promotes to
		/// SIGNED int32; int32 and wider keep their kind (a uint32 negation wraps modulo 2^32). Non-integral
		/// kinds return Invalid (no promotion applies).
		static TypeKind IntegerPromotionKind(TypeKind k);

		// Normalized integer types (snorm/unorm): stored as intN/uintN, values clamped to [-1,1]/[0,1].
		static bool IsSNormKind(TypeKind k);			static bool IsUNormKind(TypeKind k);
		static bool IsNormalizedKind(TypeKind k) { return IsSNormKind(k) || IsUNormKind(k); }
		bool IsNormalized() const { return IsNormalizedKind(m_Kind); }
		bool IsUnsignedNormalized() const { return IsUNormKind(m_Kind); }

		// True for unsigned integer kinds (plain uintN and unormN). Used to pick the correct
		// LLVM instruction variant (UIToFP/FPToUI/UDiv/URem/ZExt vs signed) - without it, large
		// unsigned values silently become negative or misdivide.
		bool IsUnsignedInteger() const;

		virtual size_t GetSize() const { return m_Size; }
		virtual size_t GetAlignment() const { return m_Alignment; }
		void SetSizeAlignment(size_t size, size_t alignment) { m_Size = size; m_Alignment = alignment; }

		bool Equals(const Type* other) const { return this == other; }

		virtual Core::String GetName() const = 0;
		virtual Core::String ToString() const { return GetName(); }

	protected:
		TypeKind m_Kind;
		size_t m_Size = 0;
		size_t m_Alignment = 1;
	};
} // namespace Scripting
