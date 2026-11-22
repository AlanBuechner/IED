// MirInterpreter's values: expressions, places and memory. A scalar is the bytes of its type; arithmetic follows the
// target's rules (integers wrap in their width, float is float, a comparison is a bool), as the emitted code does.
#include "Backend/MIR/MirInterpreter.h"
#include "Model/SymbolTable.h"
#include "Model/TypeHandle.h"
#include "Model/Types/AggregateSlots.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/PointerType.h"
#include <cmath>
#include <cstring>

namespace C4Compiler
{
	namespace
	{
		enum class Scalar { None, Bool, Signed, Unsigned, Float, Double, Pointer };

		const Type* ValueOf(const Type* type)
		{
			const Type* value = TypeHandle::UnwrapToValue(type);
			if (const auto* enumeration = dynamic_cast<const EnumType*>(value)) return TypeHandle::UnwrapToValue(enumeration->GetUnderlyingType());
			return value;
		}

		Scalar ClassOf(const Type* type)
		{
			const Type* value = ValueOf(type);
			if (!value) return Scalar::None;
			switch (value->GetKind())
			{
			case TypeKind::Bool: return Scalar::Bool;
			case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64: return Scalar::Signed;
			case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
			case TypeKind::Char8: case TypeKind::Char16: case TypeKind::Char32: return Scalar::Unsigned;
			case TypeKind::Float32: return Scalar::Float;
			case TypeKind::Float64: return Scalar::Double;
			case TypeKind::Pointer: case TypeKind::FunPtr: return Scalar::Pointer;
			default: return Scalar::None;
			}
		}

		size_t SizeOf(const Type* type)
		{
			const Type* value = ValueOf(type);
			return value ? value->GetSize() : 0;
		}

		uint64 Bits(const CtfeBytes& bytes)
		{
			uint64 bits = 0;
			std::memcpy(&bits, bytes.data(), std::min<size_t>(bytes.size(), sizeof(bits)));
			return bits;
		}

		int64 Signed(const CtfeBytes& bytes)
		{
			const uint64 bits = Bits(bytes);
			const size_t width = std::min<size_t>(bytes.size(), 8) * 8;
			if (width == 0 || width >= 64) return static_cast<int64>(bits);
			const uint64 sign = uint64(1) << (width - 1);
			return static_cast<int64>((bits ^ sign) - sign);
		}

		double Real(const CtfeBytes& bytes, Scalar kind)
		{
			if (kind == Scalar::Float && bytes.size() >= 4)
			{
				float value;
				std::memcpy(&value, bytes.data(), 4);
				return value;
			}
			double value = 0;
			if (bytes.size() >= 8) std::memcpy(&value, bytes.data(), 8);
			return value;
		}

		CtfeBytes FromBits(uint64 bits, size_t size)
		{
			CtfeBytes out(size, 0);
			std::memcpy(out.data(), &bits, std::min<size_t>(size, sizeof(bits)));
			return out;
		}

		CtfeBytes FromReal(double value, Scalar kind)
		{
			if (kind == Scalar::Float)
			{
				const float narrow = static_cast<float>(value);
				CtfeBytes out(4);
				std::memcpy(out.data(), &narrow, 4);
				return out;
			}
			CtfeBytes out(8);
			std::memcpy(out.data(), &value, 8);
			return out;
		}

		// The pointer an allocation and an offset make (allocation 0 is null's).
		uint64 PointerTo(uint64 allocation, uint64 offset) { return (allocation << 32) | offset; }
		uint64 AllocationOf(uint64 pointer) { return pointer >> 32; }
		uint64 OffsetOf(uint64 pointer) { return pointer & 0xFFFFFFFFull; }
	}

	// --- scalars as bytes -----------------------------------------------------------------------------------------

	bool MirInterpreter::Encode(const ConstantValue& value, const Type* type, CtfeBytes& out)
	{
		const Scalar kind = ClassOf(type);
		const size_t size = SizeOf(type);
		switch (kind)
		{
		case Scalar::Bool: out = FromBits(value.AsInt() != 0 ? 1 : 0, 1); return true;
		case Scalar::Signed: case Scalar::Unsigned: out = FromBits(static_cast<uint64>(value.AsInt()), size); return true;
		case Scalar::Float: case Scalar::Double:
			out = FromReal(value.kind == ConstantValue::Kind::Float64 ? value.floatValue : static_cast<double>(value.AsInt()), kind);
			return true;
		case Scalar::Pointer:
			if (value.kind != ConstantValue::Kind::None && value.AsInt() != 0) return false; // only null is a constant pointer
			out = FromBits(0, 8);
			return true;
		default: return false;
		}
	}

	ConstantValue MirInterpreter::Decode(const CtfeBytes& bytes, const Type* type)
	{
		switch (ClassOf(type))
		{
		case Scalar::Bool: return ConstantValue::Bool(Bits(bytes) != 0);
		case Scalar::Signed: return ConstantValue::Int(Signed(bytes));
		case Scalar::Unsigned: case Scalar::Pointer: return ConstantValue::Int(static_cast<int64>(Bits(bytes)));
		case Scalar::Float: case Scalar::Double: return ConstantValue::Float(Real(bytes, ClassOf(type)));
		default: return ConstantValue();
		}
	}

	// --- memory ---------------------------------------------------------------------------------------------------

	uint64 MirInterpreter::Allocate(size_t bytes)
	{
		m_Memory.emplace_back(bytes, uint8(0));
		return PointerTo(m_Memory.size() - 1, 0);
	}

	bool MirInterpreter::Read(uint64 pointer, size_t size, CtfeBytes& out, const SourceLocation& at)
	{
		const uint64 allocation = AllocationOf(pointer);
		const uint64 offset = OffsetOf(pointer);
		if (allocation == 0 || allocation >= m_Memory.size()) return Fail("compile-time evaluation read through a pointer that points nowhere", at);
		const CtfeBytes& memory = m_Memory[allocation];
		if (offset + size > memory.size()) return Fail("compile-time evaluation read past the end of an object", at);
		out.assign(memory.begin() + static_cast<std::ptrdiff_t>(offset), memory.begin() + static_cast<std::ptrdiff_t>(offset + size));
		return true;
	}

	bool MirInterpreter::Write(uint64 pointer, const CtfeBytes& value, const SourceLocation& at)
	{
		const uint64 allocation = AllocationOf(pointer);
		const uint64 offset = OffsetOf(pointer);
		if (allocation == 0 || allocation >= m_Memory.size()) return Fail("compile-time evaluation wrote through a pointer that points nowhere", at);
		CtfeBytes& memory = m_Memory[allocation];
		if (offset + value.size() > memory.size()) return Fail("compile-time evaluation wrote past the end of an object", at);
		std::copy(value.begin(), value.end(), memory.begin() + static_cast<std::ptrdiff_t>(offset));
		return true;
	}

	// --- places ---------------------------------------------------------------------------------------------------

	bool MirInterpreter::Address(Frame& frame, const LValue* place, uint64& out)
	{
		switch (place->kind)
		{
		case LValue::Kind::Slot: out = frame.slots[place->slot]; return true;
		case LValue::Kind::This:
			if (!frame.self) return Fail("compile-time evaluation used `this` outside a method", m_At);
			out = frame.self;
			return true;
		case LValue::Kind::Field:
			if (!Address(frame, place->base, out)) return false;
			out += place->field ? place->field->m_Offset : 0;
			return true;
		case LValue::Kind::Base:
			if (!Address(frame, place->base, out)) return false;
			out += static_cast<uint64>(place->baseOffset);
			return true;
		case LValue::Kind::Deref:
		{
			CtfeBytes pointer;
			if (!Eval(frame, place->addr, pointer)) return false;
			out = Bits(pointer);
			return true;
		}
		case LValue::Kind::Global: return Fail("compile-time evaluation cannot use a global variable that is not a constant", m_At);
		default: return Fail("compile-time evaluation cannot use a packed vector's lanes or an indexer", m_At);
		}
	}

	bool MirInterpreter::Load(Frame& frame, const LValue* place, const Type* type, CtfeBytes& out)
	{
		uint64 address = 0;
		return Address(frame, place, address) && Read(address, SizeOf(type), out, m_At);
	}

	bool MirInterpreter::Store(Frame& frame, const LValue* place, const CtfeBytes& value)
	{
		uint64 address = 0;
		return Address(frame, place, address) && Write(address, value, m_At);
	}

	bool MirInterpreter::Declare(Frame& frame, const LocalDeclNode& declaration)
	{
		CtfeBytes value(SizeOf(frame.fn->GetSlot(declaration.slot).type), uint8(0));
		if (declaration.init && !Eval(frame, declaration.init, value)) return false;
		return Write(frame.slots[declaration.slot], value, m_At);
	}

	// --- expressions ----------------------------------------------------------------------------------------------

	bool MirInterpreter::Eval(Frame& frame, const HIRNode* node, CtfeBytes& out)
	{
		if (!node)
		{
			out.clear();
			return true;
		}
		switch (node->kind)
		{
		case HIRNode::Kind::Constant:
			if (Encode(static_cast<const ConstantNode*>(node)->value, node->type, out)) return true;
			return Fail("compile-time evaluation cannot use a constant of type '" + (node->type ? node->type->GetName() : Core::String("?")) + "'", m_At);
		case HIRNode::Kind::Load: return Load(frame, static_cast<const LoadNode*>(node)->loc, node->type, out);
		case HIRNode::Kind::Store:
		{
			const auto* store = static_cast<const StoreNode*>(node);
			return Eval(frame, store->value, out) && Store(frame, store->target, out);
		}
		case HIRNode::Kind::AddressOf:
		{
			uint64 address = 0;
			if (!Address(frame, static_cast<const AddressOfNode*>(node)->loc, address)) return false;
			out = FromBits(address, 8);
			return true;
		}
		case HIRNode::Kind::AddressArithmetic:
		{
			const auto* arithmetic = static_cast<const AddressArithmeticNode*>(node);
			CtfeBytes base, offset;
			if (!Eval(frame, arithmetic->base, base) || !Eval(frame, arithmetic->offset, offset)) return false;
			out = FromBits(Bits(base) + static_cast<uint64>(Signed(offset)), 8);
			return true;
		}
		case HIRNode::Kind::Call: return EvalCall(frame, *static_cast<const CallNode*>(node), out);
		case HIRNode::Kind::IntrinsicOp: return EvalIntrinsic(frame, *static_cast<const IntrinsicOpNode*>(node), out);
		case HIRNode::Kind::Convert: return EvalConvert(frame, *static_cast<const ConvertNode*>(node), out);
		case HIRNode::Kind::Construct: return EvalConstruct(frame, *static_cast<const ConstructNode*>(node), out);
		case HIRNode::Kind::Sequence: return EvalSequence(frame, *static_cast<const SequenceNode*>(node), out);
		case HIRNode::Kind::Transfer: return Eval(frame, static_cast<const TransferNode*>(node)->copy, out);
		case HIRNode::Kind::Ternary:
		{
			const auto* choice = static_cast<const TernaryNode*>(node);
			CtfeBytes condition;
			if (!Eval(frame, choice->cond, condition)) return false;
			const bool taken = ClassOf(choice->cond->type) == Scalar::Float || ClassOf(choice->cond->type) == Scalar::Double
				? Real(condition, ClassOf(choice->cond->type)) != 0.0 : Bits(condition) != 0;
			return Eval(frame, taken ? choice->thenExpr : choice->elseExpr, out);
		}
		case HIRNode::Kind::Block: return RunRegion(frame, static_cast<const BlockNode*>(node));
		case HIRNode::Kind::New: return Fail("compile-time evaluation cannot allocate on the heap ('new')", m_At);
		case HIRNode::Kind::Delete: return Fail("compile-time evaluation cannot free memory ('delete')", m_At);
		default:
			return Fail("compile-time evaluation cannot run this expression (atomics, SIMD intrinsics, va_list, function values, "
				"casts that check a type at run time)", m_At);
		}
	}

	bool MirInterpreter::EvalSequence(Frame& frame, const SequenceNode& sequence, CtfeBytes& out)
	{
		out.clear();
		for (const HIRNode* step : sequence.steps)
		{
			if (step->kind == HIRNode::Kind::Block)
			{
				if (!RunRegion(frame, static_cast<const BlockNode*>(step))) return false;
				out.clear();
			}
			else if (!Eval(frame, step, out)) return false;
		}
		return true;
	}

	// A call of another function compile time can run: its arguments, then a frame of its own.
	bool MirInterpreter::EvalCall(Frame& frame, const CallNode& call, CtfeBytes& out)
	{
		if (!call.callee || call.target || call.code || call.virtualIndex >= 0)
			return Fail("compile-time evaluation cannot call through a pointer, a func value or a virtual method", m_At);
		const QueryResult<const HIRFunction*> body = m_Bodies(*call.callee);
		if (!body.Ok()) return Fail(body.message, m_At);
		uint64 self = 0;
		if (call.receiver && !Address(frame, call.receiver, self)) return false;
		std::vector<CtfeBytes> arguments(call.args.size());
		for (size_t i = 0; i < call.args.size(); ++i)
			if (!Eval(frame, call.args[i], arguments[i])) return false;
		const SourceLocation at = m_At;
		if (!Invoke(*call.callee, *body.value, self, std::move(arguments), at, out)) return false;
		m_At = at;
		return true;
	}

	// `T(...)` or `T{...}`: a zeroed object, then its constructor (one compile time can run) or its slots filled in order.
	bool MirInterpreter::EvalConstruct(Frame& frame, const ConstructNode& construct, CtfeBytes& out)
	{
		const uint64 object = Allocate(SizeOf(construct.objectType));
		if (construct.ctor)
		{
			const QueryResult<const HIRFunction*> body = m_Bodies(*construct.ctor);
			if (!body.Ok()) return Fail(body.message, m_At);
			std::vector<CtfeBytes> arguments(construct.args.size());
			for (size_t i = 0; i < construct.args.size(); ++i)
				if (!Eval(frame, construct.args[i], arguments[i])) return false;
			CtfeBytes ignored;
			const SourceLocation at = m_At;
			if (!Invoke(*construct.ctor, *body.value, object, std::move(arguments), at, ignored)) return false;
			m_At = at;
		}
		else if (construct.aggregateInit)
		{
			const auto* objectType = dynamic_cast<const ObjectType*>(ValueOf(construct.objectType));
			if (!objectType) return Fail("compile-time evaluation cannot build this value", m_At);
			const std::vector<AggregateSlots::Slot> slots = AggregateSlots::Of(*objectType);
			for (size_t i = 0; i < slots.size() && i < construct.args.size(); ++i)
			{
				if (!construct.args[i]) continue;
				if (slots[i].lane >= 0) return Fail("compile-time evaluation cannot fill a packed vector's lanes", m_At);
				CtfeBytes value;
				if (!Eval(frame, construct.args[i], value) || !Write(object + slots[i].offset, value, m_At)) return false;
			}
		}
		return Read(object, SizeOf(construct.objectType), out, m_At);
	}

	bool MirInterpreter::EvalConvert(Frame& frame, const ConvertNode& convert, CtfeBytes& out)
	{
		CtfeBytes value;
		if (!Eval(frame, convert.src, value)) return false;
		const Scalar from = ClassOf(convert.from);
		const Scalar to = ClassOf(convert.to);
		const size_t size = SizeOf(convert.to);
		const Type* toValue = ValueOf(convert.to);
		if ((toValue && toValue->IsNormalized()) || (ValueOf(convert.from) && ValueOf(convert.from)->IsNormalized()))
			return Fail("compile-time evaluation cannot convert normalized integers", m_At);
		if (convert.keepsBits || from == to)
		{
			if (from == Scalar::Pointer && to == Scalar::Pointer && ValueOf(convert.from) != ValueOf(convert.to))
			{
				const auto* source = dynamic_cast<const PointerType*>(ValueOf(convert.from));
				const auto* target = dynamic_cast<const PointerType*>(ValueOf(convert.to));
				const Type* fromPointee = source ? ValueOf(source->GetPointeeType()) : nullptr;
				const Type* toPointee = target ? ValueOf(target->GetPointeeType()) : nullptr;
				if (fromPointee && toPointee && fromPointee->GetKind() == TypeKind::Object && toPointee->GetKind() == TypeKind::Object && fromPointee != toPointee)
					return Fail("compile-time evaluation cannot convert a pointer between object types", m_At);
			}
			out = value;
			out.resize(size, 0);
			return true;
		}
		const bool fromReal = from == Scalar::Float || from == Scalar::Double;
		const bool toReal = to == Scalar::Float || to == Scalar::Double;
		if (from == Scalar::None || to == Scalar::None) return Fail("compile-time evaluation cannot convert this value", m_At);
		if (to == Scalar::Bool)
		{
			out = FromBits(fromReal ? Real(value, from) != 0.0 : Bits(value) != 0, 1);
			return true;
		}
		if (toReal)
		{
			const double real = fromReal ? Real(value, from) : from == Scalar::Signed ? static_cast<double>(Signed(value)) : static_cast<double>(Bits(value));
			out = FromReal(real, to);
			return true;
		}
		const uint64 bits = fromReal ? (to == Scalar::Signed ? static_cast<uint64>(static_cast<int64>(Real(value, from))) : static_cast<uint64>(Real(value, from)))
			: from == Scalar::Signed ? static_cast<uint64>(Signed(value)) : Bits(value);
		out = FromBits(bits, size);
		return true;
	}

	bool MirInterpreter::EvalIntrinsic(Frame& frame, const IntrinsicOpNode& op, CtfeBytes& out)
	{
		CtfeBytes left, right;
		if (!Eval(frame, op.lhs, left) || (op.rhs && !Eval(frame, op.rhs, right))) return false;
		const Scalar kind = ClassOf(op.lhs->type);
		const size_t size = SizeOf(op.lhs->type);
		const bool real = kind == Scalar::Float || kind == Scalar::Double;
		const auto truth = [&](bool value) { out = FromBits(value ? 1 : 0, 1); return true; };
		if (real)
		{
			const double a = Real(left, kind), b = op.rhs ? Real(right, kind) : 0.0;
			switch (op.op)
			{
			case IntrinsicOp::Add: out = FromReal(a + b, kind); return true;
			case IntrinsicOp::Sub: out = FromReal(a - b, kind); return true;
			case IntrinsicOp::Mul: out = FromReal(a * b, kind); return true;
			case IntrinsicOp::Div: out = FromReal(a / b, kind); return true;
			case IntrinsicOp::Mod: out = FromReal(std::fmod(a, b), kind); return true;
			case IntrinsicOp::Pow: out = FromReal(std::pow(a, b), kind); return true;
			case IntrinsicOp::Negate: out = FromReal(-a, kind); return true;
			case IntrinsicOp::Eq: return truth(a == b);
			case IntrinsicOp::Ne: return truth(a != b);
			case IntrinsicOp::Lt: return truth(a < b);
			case IntrinsicOp::Gt: return truth(a > b);
			case IntrinsicOp::Le: return truth(a <= b);
			case IntrinsicOp::Ge: return truth(a >= b);
			case IntrinsicOp::BoolNot: return truth(a == 0.0);
			case IntrinsicOp::Likely: case IntrinsicOp::Unlikely: out = left; return true;
			default: return Fail("compile-time evaluation cannot run this operator on floating-point values", m_At);
			}
		}
		const bool isSigned = kind == Scalar::Signed;
		const uint64 a = Bits(left), b = op.rhs ? Bits(right) : 0;
		const int64 sa = Signed(left), sb = op.rhs ? Signed(right) : 0;
		const uint64 width = size * 8;
		const auto value = [&](uint64 bits) { out = FromBits(bits, size); return true; };
		switch (op.op)
		{
		case IntrinsicOp::Add: return value(a + b);
		case IntrinsicOp::Sub: return value(a - b);
		case IntrinsicOp::Mul: return value(a * b);
		case IntrinsicOp::Div:
		case IntrinsicOp::Mod:
			if (b == 0) return Fail("compile-time evaluation divided by zero", m_At);
			if (isSigned && sb == -1) return value(op.op == IntrinsicOp::Div ? static_cast<uint64>(0) - a : 0); // INT_MIN / -1 wraps
			if (isSigned) return value(static_cast<uint64>(op.op == IntrinsicOp::Div ? sa / sb : sa % sb));
			return value(op.op == IntrinsicOp::Div ? a / b : a % b);
		case IntrinsicOp::Pow:
		{
			uint64 result = 1;
			if (isSigned && sb < 0) return value(sa == 1 ? 1 : sa == -1 ? static_cast<uint64>((sb & 1) ? -1 : 1) : 0);
			for (uint64 e = b, base = a; e; e >>= 1, base *= base)
				if (e & 1) result *= base;
			return value(result);
		}
		case IntrinsicOp::Eq: return truth(a == b);
		case IntrinsicOp::Ne: return truth(a != b);
		case IntrinsicOp::Lt: return truth(isSigned ? sa < sb : a < b);
		case IntrinsicOp::Gt: return truth(isSigned ? sa > sb : a > b);
		case IntrinsicOp::Le: return truth(isSigned ? sa <= sb : a <= b);
		case IntrinsicOp::Ge: return truth(isSigned ? sa >= sb : a >= b);
		case IntrinsicOp::BitAnd: return value(a & b);
		case IntrinsicOp::BitOr: return value(a | b);
		case IntrinsicOp::BitXor: return value(a ^ b);
		case IntrinsicOp::Shl:
		case IntrinsicOp::Shr:
			if (b >= width) // a shift by the width or more has no defined value: an error, not a guess
				return Fail("compile-time evaluation shifted a " + Core::String(std::to_string(width).c_str()) + "-bit value by " +
					Core::String(std::to_string(sb).c_str()) + " bits", m_At);
			if (op.op == IntrinsicOp::Shl) return value(a << b);
			return value(isSigned ? static_cast<uint64>(sa >> b) : a >> b);
		case IntrinsicOp::Negate: return value(static_cast<uint64>(0) - a);
		case IntrinsicOp::BitNot: return value(~a);
		case IntrinsicOp::BoolNot: return truth(a == 0);
		case IntrinsicOp::Likely: case IntrinsicOp::Unlikely: out = left; return true;
		default: return Fail("compile-time evaluation cannot run this bit intrinsic", m_At);
		}
	}
} // namespace C4Compiler
