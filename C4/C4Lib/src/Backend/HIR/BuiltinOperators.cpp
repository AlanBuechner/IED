#include "Backend/HIR/BuiltinOperators.h"
#include "Model/TypeHandle.h"
#include "Model/Types/SwizzleType.h"

namespace Scripting::BuiltinOperators
{
	namespace
	{
		const Type* Element(const Type* t)
		{
			return t && t->GetKind() == TypeKind::Swizzle ? static_cast<const SwizzleType*>(t)->GetElementType() : t;
		}

		bool Numeric(const Type* t) { return Element(t) && Element(t)->IsNumeric(); }
		bool Integral(const Type* t) { return Element(t) && Element(t)->IsIntegral() && !Element(t)->IsNormalized(); }
		bool Normalized(const Type* t) { return Element(t) && Element(t)->IsNormalized(); }

		Core::String FractionProblem(const Core::String& tok, const Type* t)
		{
			return "operator '" + tok + "' does not take a '" + t->GetName() +
				"': a normalized value is a fraction; read its stored integer with (int32)x";
		}
		bool PointerLike(const Type* t) { return t && (t->GetKind() == TypeKind::Pointer || t->GetKind() == TypeKind::FunPtr); }
		bool Mask(const Type* t) { return t && t->GetKind() == TypeKind::Swizzle && Element(t)->GetKind() == TypeKind::Bool; }

		Core::String MaskProblem(const Core::String& tok, const Type* mask)
		{
			return "operator '" + tok + "' does not take a '" + mask->GetName() +
				"': a packed bool vector is a mask, combined with '&', '|', '^' and '!' and compared with '==' and '!='";
		}

		bool IsOneOf(const Core::String& tok, std::initializer_list<const char*> tokens)
		{
			for (const char* t : tokens)
				if (tok == t) return true;
			return false;
		}

		Core::String Needs(const Core::String& tok, const char* what, const Type* l, const Type* r)
		{
			return "operator '" + tok + "' needs " + what + " operands, not '" + l->GetName() + "' and '" + r->GetName() + "'";
		}
	}

	Core::String BinaryProblem(const Core::String& tok, const Type* l, const Type* r)
	{
		if ((Mask(l) || Mask(r)) && !IsOneOf(tok, { "&", "|", "^", "==", "!=" })) return MaskProblem(tok, Mask(l) ? l : r);
		if (IsOneOf(tok, { "+", "-", "*", "/", "%", "**" }))
			return Numeric(l) && Numeric(r) ? "" : Needs(tok, "numeric", l, r);
		if (IsOneOf(tok, { "&", "|", "^", "<<", ">>" }))
		{
			if (Normalized(l) || Normalized(r)) return FractionProblem(tok, Normalized(l) ? l : r);
			return Integral(l) && Integral(r) ? "" : Needs(tok, "integer", l, r);
		}
		if (IsOneOf(tok, { "==", "!=", "<", ">", "<=", ">=" }))
			return (Numeric(l) && Numeric(r)) || (PointerLike(l) && PointerLike(r)) ? ""
				: "operator '" + tok + "' cannot compare a '" + l->GetName() + "' and a '" + r->GetName() + "'";
		return "";
	}

	Core::String MissingOperator(const Core::String& tok, const Type* l, const Type* r)
	{
		Core::String message = "no operator '" + tok + "' takes a '" + l->GetName() + "' on the left and a '" + r->GetName() +
			"' on the right: an operator is a method of its left operand's type";
		if (l->GetKind() != TypeKind::Object)
			message += "; for a '" + l->GetName() + "' on the left, declare it as an extension method that 'extends " + l->GetName() + "'";
		return message;
	}

	Core::String UnaryProblem(const Core::String& tok, const Type* t)
	{
		if (Mask(t)) return tok == "!" ? "" : MaskProblem(tok, t);
		if (tok == "-" || tok == "+")
			return Numeric(t) ? "" : "unary '" + tok + "' needs a numeric operand, not '" + t->GetName() + "'";
		if (Normalized(t) && (tok == "~" || tok == "++" || tok == "--"))
			return tok == "~" ? FractionProblem(tok, t)
				: "operator '" + tok + "' does not take a '" + t->GetName() + "': a normalized value is a fraction; add to it with x += 0.1f";
		if (tok == "~")
			return Integral(t) ? "" : "operator '~' needs an integer operand, not '" + t->GetName() + "'";
		if (tok == "!")
			return TypeHandle::HasTruthValue(t) ? "" : "operator '!' needs a value with a truth value, not '" + t->GetName() + "'";
		if (tok == "++" || tok == "--")
			return (Numeric(t) && t->GetKind() != TypeKind::Bool) || t->GetKind() == TypeKind::Pointer ? ""
				: "operator '" + tok + "' needs a number or a pointer, not '" + t->GetName() + "'";
		return "";
	}
} // namespace Scripting::BuiltinOperators
