#include "Model/ConstConversions.h"
#include "Model/ConstEvaluator.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include <cmath>
#include <cstdio>

namespace Scripting
{
	namespace
	{
		using Result = QueryResult<ConstantValue>;
		constexpr uint32 kBitsPerByte = 8;
		constexpr uint32 kWidestInteger = 64;

		// `1000` and `2.5`, not `1000.000000`.
		Core::String Shown(double value)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.15g", value);
			return text;
		}

		bool IsSignedInteger(TypeKind kind) { return kind >= TypeKind::Int8 && kind <= TypeKind::Int64; }
		bool IsUnsignedInteger(TypeKind kind)
		{
			return (kind >= TypeKind::UInt8 && kind <= TypeKind::UInt64) || kind == TypeKind::Char8 || kind == TypeKind::Char16 ||
				kind == TypeKind::Char32;
		}

		// The low `width` bits of `bits`, sign-extended when the target is signed.
		int64 Wrap(uint64 bits, uint32 width, bool isSigned)
		{
			if (width >= kWidestInteger) return static_cast<int64>(bits);
			const uint64 mask = (uint64(1) << width) - 1;
			bits &= mask;
			if (isSigned && ((bits >> (width - 1)) & 1)) bits |= ~mask;
			return static_cast<int64>(bits);
		}

		Result FromFloat(double value, const Type& target, uint32 width, bool isSigned)
		{
			const double whole = std::trunc(value);
			const double low = isSigned ? -std::ldexp(1.0, static_cast<int>(width) - 1) : 0.0;
			const double end = isSigned ? std::ldexp(1.0, static_cast<int>(width) - 1) : std::ldexp(1.0, static_cast<int>(width));
			if (std::isnan(whole) || whole < low || whole >= end)
				return Result::Fail(QueryError::NotAValue, "the value " + Shown(value) + " is outside the range of '" + target.GetName() + "'");
			if (!isSigned && whole >= std::ldexp(1.0, kWidestInteger - 1)) return Result::Success(ConstantValue::Int(static_cast<int64>(static_cast<uint64>(whole))));
			return Result::Success(ConstantValue::Int(static_cast<int64>(whole)));
		}

		Result ToInteger(const ConstantValue& value, const Type& target)
		{
			const uint32 width = static_cast<uint32>(target.GetSize()) * kBitsPerByte;
			const bool isSigned = IsSignedInteger(target.GetKind());
			if (value.kind == ConstantValue::Kind::Float64) return FromFloat(value.floatValue, target, width, isSigned);
			return Result::Success(ConstantValue::Int(Wrap(static_cast<uint64>(value.AsInt()), width, isSigned)));
		}

		Result ToFloat(const ConstantValue& value, TypeKind kind)
		{
			const double wide = value.kind == ConstantValue::Kind::Float64 ? value.floatValue : static_cast<double>(value.AsInt());
			return Result::Success(ConstantValue::Float(kind == TypeKind::Float32 ? static_cast<double>(static_cast<float>(wide)) : wide));
		}
	}

	QueryResult<ConstantValue> ConvertConstant(const ConstantValue& value, const Type* target)
	{
		const Type* type = TypeHandle::UnwrapToValue(target);
		if (type && type->GetKind() == TypeKind::Enum)
		{
			const Type* underlying = static_cast<const EnumType*>(type)->GetUnderlyingType();
			if (!underlying) return Result::Fail(QueryError::NotAValue, "the enum '" + type->GetName() + "' has no underlying type yet");
			type = underlying;
		}
		if (!type) return Result::Fail(QueryError::NotAValue, "the cast has no target type");
		if (value.kind != ConstantValue::Kind::Int64 && value.kind != ConstantValue::Kind::Float64 && value.kind != ConstantValue::Kind::Bool)
			return Result::Fail(QueryError::NotAValue, "only a number or a bool converts in a constant");
		const TypeKind kind = type->GetKind();
		if (kind == TypeKind::Bool)
		{
			const bool truth = value.kind == ConstantValue::Kind::Float64 ? value.floatValue != 0.0 : value.AsInt() != 0;
			return Result::Success(ConstantValue::Bool(truth));
		}
		if (IsSignedInteger(kind) || IsUnsignedInteger(kind)) return ToInteger(value, *type);
		if (kind == TypeKind::Float32 || kind == TypeKind::Float64) return ToFloat(value, kind);
		if (type->IsNormalized())
			return Result::Fail(QueryError::NotAValue, "'" + type->GetName() + "' is a normalized value, a fraction the constant does not fold over");
		return Result::Fail(QueryError::NotAValue, "a cast to '" + type->GetName() + "' is not a constant");
	}

	QueryResult<ConstantValue> StoredConstant(const ConstantValue& value, const Type* target)
	{
		const Type* type = target ? TypeHandle::UnwrapToValue(target) : nullptr;
		if (!type) return Result::Fail(QueryError::NotAValue, "the variable has no type yet");
		if (type->GetKind() != TypeKind::Pointer && type->GetKind() != TypeKind::FunPtr) return ConvertConstant(value, type);
		const bool null = value.kind == ConstantValue::Kind::Int64 && value.intValue == 0;
		if (null || value.kind == ConstantValue::Kind::String) return Result::Success(value);
		return Result::Fail(QueryError::NotAValue, "a pointer starts as a constant only from null or a string");
	}

	// A C-style cast or a static_cast folds through ConvertConstant; the checked and reinterpreting casts never do.
	QueryResult<ConstantValue> ConstEvaluator::FoldCast(const CastExpressionNode& cast, uint32 unitId, Visiting& visiting, Scope* bindingScope) const
	{
		using Kind = CastExpressionNode::CastKind;
		if (cast.GetKind() != Kind::CStyle && cast.GetKind() != Kind::StaticCast)
			return Result::Fail(QueryError::NotAValue, "'" + cast.ToString() + "' is not a constant: only a C-style cast or a static_cast folds");
		auto operand = EvaluateInner(cast.GetOperand().Get(), unitId, visiting, bindingScope);
		if (!operand.Ok()) return operand;
		auto target = m_Model.ResolveTypeInternal(*cast.GetTargetType(), unitId, bindingScope, m_Context ? m_Context : m_Model.m_Symbols->GetRoot());
		if (!target.Ok()) return Result::Fail(target.error, target.message);
		return ConvertConstant(operand.value, target.value);
	}
}
