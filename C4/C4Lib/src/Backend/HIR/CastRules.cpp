#include "Backend/HIR/CastRules.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/SwizzleType.h"

namespace Scripting::CastRules
{
	namespace
	{
		using Kind = CastExpressionNode::CastKind;

		const char* Keyword(Kind kind)
		{
			switch (kind)
			{
			case Kind::StaticCast: return "static_cast";
			case Kind::ReinterpretCast: return "reinterpret_cast";
			case Kind::ConstCast: return "const_cast";
			default: return "a cast";
			}
		}

		bool IsNumber(const Type* t) { return t->IsNumeric() || t->GetKind() == TypeKind::Bool || t->GetKind() == TypeKind::Enum; }
		bool IsPointer(const Type* t) { return t->GetKind() == TypeKind::Pointer || t->GetKind() == TypeKind::FunPtr; }
		const Type* Pointee(const Type* t) { return t->GetKind() == TypeKind::Pointer ? TypeHandle::UnwrapToValue(static_cast<const PointerType*>(t)->GetPointeeType()) : nullptr; }
		bool PointsToConst(const Type* t) { return t->GetKind() == TypeKind::Pointer && static_cast<const PointerType*>(t)->IsConstReferent(); }

		// `const T*` to `T*`, at any pointer level.
		bool DropsConst(const Type* from, const Type* to)
		{
			if (from->GetKind() != TypeKind::Pointer || to->GetKind() != TypeKind::Pointer) return false;
			if (PointsToConst(from) && !PointsToConst(to)) return true;
			const Type* a = Pointee(from);
			const Type* b = Pointee(to);
			return a && b && DropsConst(a, b);
		}

		// The same type once every pointer level's const is left out.
		bool SameButConst(const Type* a, const Type* b)
		{
			if (a == b) return true;
			if (a->GetKind() != TypeKind::Pointer || b->GetKind() != TypeKind::Pointer) return false;
			const Type* x = Pointee(a);
			const Type* y = Pointee(b);
			return x && y && SameButConst(x, y);
		}

		bool Related(const Type* a, const Type* b)
		{
			const auto* x = dynamic_cast<const ObjectType*>(a);
			const auto* y = dynamic_cast<const ObjectType*>(b);
			return x && y && (ClassHierarchy::DerivesFrom(x, y) || ClassHierarchy::DerivesFrom(y, x));
		}

		Core::String Cannot(Kind kind, const Type* from, const Type* to, const Core::String& why)
		{
			return Core::String(Keyword(kind)) + " cannot convert '" + QualifiedName(from) + "' to '" + QualifiedName(to) + "': " + why;
		}

		bool IsPacked(const Type* t)
		{
			const auto* lanes = dynamic_cast<const SwizzleType*>(t);
			return lanes && lanes->IsPacked();
		}

		bool IsLane(const Type* t) { return t->IsNumeric() && !t->IsNormalized() && t->GetKind() != TypeKind::Enum; }

		bool HasBits(const Type* t) { return t->IsNumeric() && t->GetKind() != TypeKind::Bool && t->GetKind() != TypeKind::Enum; }

		bool IsMask(const Type* t) { return static_cast<const SwizzleType*>(t)->GetElementType()->GetKind() == TypeKind::Bool; }

		// A pure packed vector object (Math::FVector4) is its lanes: a cast to a packed vector reads them (D-62).
		const Type* LanesOrSelf(const Type* t)
		{
			const auto* object = dynamic_cast<const ObjectType*>(t);
			return object && object->IsPurePackedVector() ? object->GetPackedVectorSwizzle() : t;
		}

		// D-62: lane by lane between vectors of one lane count with number or bool lanes; a number broadcasts.
		Core::String LaneCast(Kind kind, const Type* from, const Type* to)
		{
			if (dynamic_cast<const ObjectType*>(to))
				return Cannot(kind, from, to, "construct one from the lanes, " + QualifiedName(to) + "(v), or convert to its lanes' type first");
			if (!IsPacked(to)) return Cannot(kind, from, to, "a vector does not convert to one value: read a lane, v[i]");
			const Type* element = static_cast<const SwizzleType*>(to)->GetElementType();
			if (!IsPacked(from))
				return IsLane(from) && IsLane(element) ? "" : Cannot(kind, from, to, "a vector is made from a number, which goes to every lane");
			const auto* source = static_cast<const SwizzleType*>(from);
			if (!IsLane(source->GetElementType()) || !IsLane(element)) return Cannot(kind, from, to, "lanes convert between numbers and bools");
			if (source->GetLaneCount() != static_cast<const SwizzleType*>(to)->GetLaneCount())
				return Cannot(kind, from, to, "a cast converts lane by lane, so the lane counts must match (reinterpret_cast keeps the bits of a vector of the same size)");
			return "";
		}

		Core::String BitCast(const Type* from, const Type* to)
		{
			if (!IsPacked(from) || !IsPacked(to)) return Cannot(Kind::ReinterpretCast, from, to, "reinterpret_cast keeps the bits of a vector only as another vector");
			if (IsMask(from) || IsMask(to))
				return Cannot(Kind::ReinterpretCast, from, to, "a mask's lanes have no bits to keep: convert it lane by lane, (packed int32[N])mask");
			if (from->GetSize() != to->GetSize())
				return Cannot(Kind::ReinterpretCast, from, to, "reinterpret_cast keeps the bits of a vector of the same size, and these are " +
					std::to_string(from->GetSize()) + " and " + std::to_string(to->GetSize()) + " bytes");
			return "";
		}

		// D-64: a number's bits, read as another number of the same size.
		Core::String NumberBits(const Type* from, const Type* to)
		{
			if (!HasBits(from) || !HasBits(to))
				return Cannot(Kind::ReinterpretCast, from, to, "a bool or an enum has no bits to keep: convert it with static_cast");
			if (from->GetSize() != to->GetSize())
				return Cannot(Kind::ReinterpretCast, from, to, "reinterpret_cast keeps the bits of a number of the same size, and these are " +
					std::to_string(from->GetSize()) + " and " + std::to_string(to->GetSize()) + " bytes");
			return "";
		}

		Core::String StaticCast(const Type* from, const Type* to)
		{
			if (to->GetKind() == TypeKind::Void) return ""; // `(void)x` discards the value
			if (IsPacked(from) || IsPacked(to)) return LaneCast(Kind::StaticCast, from, to);
			if (IsNumber(from) && IsNumber(to)) return "";
			if (from->GetKind() == TypeKind::Pointer && to->GetKind() == TypeKind::Pointer)
			{
				if (DropsConst(from, to)) return Cannot(Kind::StaticCast, from, to, "it removes const (const_cast does that)");
				const Type* a = Pointee(from);
				const Type* b = Pointee(to);
				if (a == b || a->GetKind() == TypeKind::Void || b->GetKind() == TypeKind::Void || Related(a, b)) return "";
				return Cannot(Kind::StaticCast, from, to, "the types are unrelated (reinterpret_cast reinterprets the bits)");
			}
			if (IsPointer(from) != IsPointer(to) && (from->IsIntegral() || to->IsIntegral()))
				return Cannot(Kind::StaticCast, from, to, "a pointer and an integer convert with reinterpret_cast");
			const auto* derived = dynamic_cast<const ObjectType*>(from);
			const auto* base = dynamic_cast<const ObjectType*>(to);
			if (derived && base && ClassHierarchy::DerivesFrom(derived, base)) return "";
			return Cannot(Kind::StaticCast, from, to, "static_cast converts numbers, related objects and pointers to them");
		}

		Core::String ReinterpretCast(const Type* from, const Type* to)
		{
			if (IsPacked(from) || IsPacked(to)) return BitCast(from, to);
			if (IsPointer(from) && IsPointer(to))
				return DropsConst(from, to) ? Cannot(Kind::ReinterpretCast, from, to, "it removes const (const_cast does that)") : "";
			if ((IsPointer(from) && to->IsIntegral()) || (from->IsIntegral() && IsPointer(to))) return "";
			if (from->IsNumeric() && to->IsNumeric()) return NumberBits(from, to);
			return Cannot(Kind::ReinterpretCast, from, to,
				"reinterpret_cast converts pointers to pointers, between a pointer and an integer, and keeps the bits of a number or a vector");
		}

		Core::String ConstCast(const Type* from, const Type* to)
		{
			if (from->GetKind() == TypeKind::Pointer && to->GetKind() == TypeKind::Pointer && SameButConst(from, to)) return "";
			return Cannot(Kind::ConstCast, from, to, "const_cast only adds or removes const");
		}

		// `(T)x` is whichever of static_cast, const_cast and reinterpret_cast converts, as in C++: a number, any pointer
		// to any pointer (dropping const too), a pointer and an integer, or a derived object to its base.
		Core::String CStyleCast(const Type* from, const Type* to)
		{
			if (IsPacked(from) || IsPacked(to)) return LaneCast(Kind::CStyle, from, to); // bits only through reinterpret_cast
			if (StaticCast(from, to).empty() || ReinterpretCast(from, to).empty()) return "";
			if (IsPointer(from) && IsPointer(to)) return "";
			if (IsPointer(from) && to->GetKind() == TypeKind::Bool) return "";
			if (dynamic_cast<const ObjectType*>(from)) return Cannot(Kind::CStyle, from, to, "an object converts through its [operator(\"cast\")] method");
			if (dynamic_cast<const ObjectType*>(to)) return Cannot(Kind::CStyle, from, to, "construct one with " + QualifiedName(to) + "(...)");
			return Cannot(Kind::CStyle, from, to, "a cast converts numbers, pointers and related objects");
		}
	}

	Core::String WhyRejected(CastExpressionNode::CastKind kind, const Type* from, const Type* to)
	{
		from = TypeHandle::UnwrapToValue(from);
		to = TypeHandle::UnwrapToValue(to);
		if (from && to && IsPacked(to)) from = LanesOrSelf(from);
		if (!from || !to || from == to) return "";
		switch (kind)
		{
		case Kind::StaticCast: return StaticCast(from, to);
		case Kind::ReinterpretCast: return ReinterpretCast(from, to);
		case Kind::ConstCast: return ConstCast(from, to);
		case Kind::CStyle: return CStyleCast(from, to);
		default: return "";
		}
	}

	bool KeepsBits(CastExpressionNode::CastKind kind, const Type* from, const Type* to)
	{
		from = TypeHandle::UnwrapToValue(from);
		to = TypeHandle::UnwrapToValue(to);
		if (kind != Kind::ReinterpretCast || !from || !to) return false;
		if (IsPacked(to)) return IsPacked(LanesOrSelf(from));
		return HasBits(from) && HasBits(to);
	}
}
