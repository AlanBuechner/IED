#include "Backend/HIR/ConstantImage.h"
#include "Model/ConstConversions.h"
#include "Model/SymbolTable.h"
#include "Model/TypeHandle.h"
#include <cstring>

namespace Scripting
{
	namespace
	{
		// The constant integer `node` computes: a constant, one widened, or a sum or product of them (an element's
		// byte offset).
		std::optional<int64> IntegerOf(const HIRNode* node)
		{
			if (const auto* constant = dynamic_cast<const ConstantNode*>(node))
				return constant->value.kind == ConstantValue::Kind::Int64 ? std::optional<int64>(constant->value.intValue) : std::nullopt;
			if (const auto* convert = dynamic_cast<const ConvertNode*>(node))
				return convert->to && convert->to->GetSize() == sizeof(int64) && convert->to->IsIntegral() ? IntegerOf(convert->src) : std::nullopt;
			const auto* op = dynamic_cast<const IntrinsicOpNode*>(node);
			if (!op || (op->op != IntrinsicOp::Mul && op->op != IntrinsicOp::Add)) return std::nullopt;
			const std::optional<int64> lhs = IntegerOf(op->lhs);
			const std::optional<int64> rhs = IntegerOf(op->rhs);
			if (!lhs || !rhs) return std::nullopt;
			return op->op == IntrinsicOp::Mul ? *lhs * *rhs : *lhs + *rhs;
		}

		// The constant `node` is, before it is stored: a constant, or a conversion of one (a string literal or null to
		// another pointer type, as the store's own conversion allows).
		std::optional<ConstantValue> ConstantOf(const HIRNode* node);

		// `-c` or `~c` of a constant, in the operation's type (a negative literal is a negation).
		std::optional<ConstantValue> UnaryOf(const IntrinsicOpNode& op)
		{
			std::optional<ConstantValue> operand = ConstantOf(op.lhs);
			if (!operand || operand->IsNone()) return std::nullopt;
			auto value = ConvertConstant(*operand, op.type);
			if (!value.Ok()) return std::nullopt;
			ConstantValue result = value.value;
			const bool integer = result.kind == ConstantValue::Kind::Int64;
			if (op.op == IntrinsicOp::Negate && integer) result.intValue = static_cast<int64>(0 - static_cast<uint64>(result.intValue));
			else if (op.op == IntrinsicOp::Negate && result.kind == ConstantValue::Kind::Float64) result.floatValue = -result.floatValue;
			else if (op.op == IntrinsicOp::BitNot && integer) result.intValue = ~result.intValue;
			else return std::nullopt;
			auto wrapped = ConvertConstant(result, op.type);
			return wrapped.Ok() ? std::optional<ConstantValue>(wrapped.value) : std::nullopt;
		}

		std::optional<ConstantValue> ConstantOf(const HIRNode* node)
		{
			if (const auto* constant = dynamic_cast<const ConstantNode*>(node)) return constant->value;
			if (const auto* op = dynamic_cast<const IntrinsicOpNode*>(node); op && !op->rhs) return UnaryOf(*op);
			const auto* convert = dynamic_cast<const ConvertNode*>(node);
			if (!convert || convert->keepsBits) return std::nullopt;
			std::optional<ConstantValue> source = ConstantOf(convert->src);
			if (!source || source->IsNone()) return std::nullopt;
			const Type* to = TypeHandle::UnwrapToValue(convert->to);
			auto converted = to && to->GetKind() == TypeKind::Pointer ? StoredConstant(*source, to) : ConvertConstant(*source, to);
			return converted.Ok() ? std::optional<ConstantValue>(converted.value) : std::nullopt;
		}

		// The bits a scalar of `type` holds for `value`, or nothing when it is no number.
		std::optional<uint64> BitsOf(const ConstantValue& value, const Type* type)
		{
			switch (type->GetKind())
			{
			case TypeKind::Bool:
				return value.kind == ConstantValue::Kind::Bool ? std::optional<uint64>(value.booleanValue ? 1 : 0) : std::nullopt;
			case TypeKind::Float32:
			{
				if (value.kind != ConstantValue::Kind::Float64) return std::nullopt;
				const float single = static_cast<float>(value.floatValue);
				uint32 bits = 0;
				std::memcpy(&bits, &single, sizeof(bits));
				return bits;
			}
			case TypeKind::Float64:
			{
				if (value.kind != ConstantValue::Kind::Float64) return std::nullopt;
				uint64 bits = 0;
				std::memcpy(&bits, &value.floatValue, sizeof(bits));
				return bits;
			}
			case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
			case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
			case TypeKind::Char8: case TypeKind::Char16: case TypeKind::Char32: case TypeKind::Enum:
				return value.kind == ConstantValue::Kind::Int64 ? std::optional<uint64>(static_cast<uint64>(value.intValue)) : std::nullopt;
			default:
				return std::nullopt;
			}
		}

		class Image
		{
		public:
			Image(const SymbolInfo& global, uint64 size) : m_Global(global) { m_Data.bytes.assign(size, 0); }

			bool Run(const HIRNode* step)
			{
				if (const auto* block = dynamic_cast<const BlockNode*>(step))
				{
					for (const HIRNode* inner : block->stmts)
						if (!Run(inner)) return false;
					return true;
				}
				if (const auto* statement = dynamic_cast<const ExprStmtNode*>(step)) return Run(statement->expr);
				const auto* store = dynamic_cast<const StoreNode*>(step);
				return store && Store(*store);
			}

			ConstantData Take() { return std::move(m_Data); }

		private:
			// Where `location` is in the global, in bytes; nothing when it is not a fixed part of it.
			std::optional<int64> OffsetOf(const LValue* location) const
			{
				switch (location->kind)
				{
				case LValue::Kind::Global:
					return location->sym == &m_Global ? std::optional<int64>(0) : std::nullopt;
				case LValue::Kind::Field:
				{
					const std::optional<int64> object = location->field ? OffsetOf(location->base) : std::nullopt;
					return object ? std::optional<int64>(*object + static_cast<int64>(location->field->m_Offset)) : std::nullopt;
				}
				case LValue::Kind::Base:
				{
					const std::optional<int64> object = OffsetOf(location->base);
					return object ? std::optional<int64>(*object + location->baseOffset) : std::nullopt;
				}
				case LValue::Kind::Deref:
					return AddressOffset(location->addr);
				default:
					return std::nullopt;
				}
			}

			// `&part` or `&part + n`, as an offset in the global.
			std::optional<int64> AddressOffset(const HIRNode* address) const
			{
				if (const auto* of = dynamic_cast<const AddressOfNode*>(address)) return OffsetOf(of->loc);
				const auto* moved = dynamic_cast<const AddressArithmeticNode*>(address);
				if (!moved) return std::nullopt;
				const std::optional<int64> base = AddressOffset(moved->base);
				const std::optional<int64> step = IntegerOf(moved->offset);
				return base && step ? std::optional<int64>(*base + *step) : std::nullopt;
			}

			bool Store(const StoreNode& store)
			{
				const std::optional<int64> offset = OffsetOf(store.target);
				const Type* type = TypeHandle::UnwrapToValue(store.target->type);
				if (!offset || !type || *offset < 0 || static_cast<uint64>(*offset) + type->GetSize() > m_Data.bytes.size()) return false;
				std::optional<ConstantValue> value = ConstantOf(store.value);
				if (!value) return false;
				Clear(*offset, type->GetSize());
				if (value->IsNone()) return true; // the zero of the whole part
				auto stored = StoredConstant(*value, type);
				if (!stored.Ok()) return false;
				if (stored.value.kind == ConstantValue::Kind::String && type->GetKind() == TypeKind::Pointer)
				{
					m_Data.strings[static_cast<uint64>(*offset)] = stored.value;
					return true;
				}
				const std::optional<uint64> bits = type->GetKind() == TypeKind::Pointer ? NullBits(stored.value) : BitsOf(stored.value, type);
				if (!bits || type->GetSize() > sizeof(uint64)) return false;
				for (uint64 i = 0; i < type->GetSize(); ++i) m_Data.bytes[*offset + i] = static_cast<uint8>(*bits >> (8 * i));
				return true;
			}

			// The part a store replaces: its bytes and any string a pointer in it held.
			void Clear(int64 offset, uint64 size)
			{
				std::memset(m_Data.bytes.data() + offset, 0, size);
				m_Data.strings.erase(m_Data.strings.lower_bound(static_cast<uint64>(offset)), m_Data.strings.lower_bound(static_cast<uint64>(offset) + size));
			}

			static std::optional<uint64> NullBits(const ConstantValue& value)
			{
				return value.kind == ConstantValue::Kind::Int64 && value.intValue == 0 ? std::optional<uint64>(0) : std::nullopt;
			}

			const SymbolInfo& m_Global;
			ConstantData m_Data;
		};
	}

	std::optional<ConstantData> ConstantImage(const BlockNode& start, const SymbolInfo& global, uint64 size)
	{
		Image image(global, size);
		if (!image.Run(&start)) return std::nullopt;
		return image.Take();
	}
} // namespace Scripting
