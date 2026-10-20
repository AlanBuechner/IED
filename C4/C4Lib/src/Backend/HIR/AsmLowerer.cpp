#include "Backend/HIR/AsmLowerer.h"
#include "Backend/HIR/AsmTemplate.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/Lifecycle.h"
#include "Model/ConstEvaluator.h"
#include "Model/TypeHandle.h"
#include "Model/Types/SwizzleType.h"
#include "Parser/Nodes/Statements/AsmStatementNode.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		Core::String Described(const AsmOperand& op)
		{
			return op.name.empty() ? Core::String("an unnamed operand") : Core::String("'" + op.name + "'");
		}

		std::string Join(const std::vector<std::string>& parts)
		{
			std::string out;
			for (const std::string& part : parts)
				out += (out.empty() ? "" : ",") + part;
			return out;
		}

		bool IsNumber(const std::string& s)
		{
			return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
		}

		// The bytes a vector register class holds.
		size_t ClassBytes(const std::string& registerClass)
		{
			return registerClass == "xmm" ? 16 : registerClass == "ymm" ? 32 : 64;
		}
	}

	QueryResult<void> AsmLowerer::Lower(const AsmStatementNode* n, BlockNode* block)
	{
		auto options = ReadOptions(n);
		if (!options.Ok()) return QueryResult<void>::Fail(options.error, options.message);
		return Lifecycle(m_Ctx).LowerStatement(block, [&]() { return LowerOperands(n, options.value, block); });
	}

	QueryResult<AsmLowerer::Options> AsmLowerer::ReadOptions(const AsmStatementNode* n)
	{
		using Result = QueryResult<Options>;
		Options options;
		for (const Token& token : n->GetOptions())
		{
			const Core::String& name = token.GetText();
			if (name == "att_syntax") options.att = true;
			else if (name == "nomem") options.nomem = true;
			else if (name == "readonly") options.readonly = true;
			else if (name == "pure") options.pure = true;
			else if (name == "nostack") options.nostack = true;
			else if (name == "preserves_flags") options.preservesFlags = true;
			else if (name == "noreturn") options.noReturn = true;
			else
				return m_Ctx.Fail(n, Result::Fail(QueryError::NotAValue, "'" + name + "' is not an asm option: use att_syntax, nomem, readonly, "
					"pure, nostack, preserves_flags or noreturn"));
		}
		if (options.nomem && options.readonly)
			return m_Ctx.Fail(n, Result::Fail(QueryError::NotAValue, "an asm block is 'nomem' (it touches no memory) or 'readonly' (it only reads "
				"memory), not both"));
		if (options.pure && !options.nomem && !options.readonly)
			return m_Ctx.Fail(n, Result::Fail(QueryError::NotAValue, "a 'pure' asm block also says what it does to memory: add 'nomem' or "
				"'readonly'"));
		return Result(options);
	}

	QueryResult<void> AsmLowerer::LowerOperands(const AsmStatementNode* n, const Options& options, BlockNode* block)
	{
		const auto& operands = n->GetOperands();
		for (size_t i = 0; i < static_cast<size_t>(operands.Count()); ++i)
			for (size_t j = 0; j < i; ++j)
				if (!operands[i].name.empty() && operands[i].name == operands[j].name)
					return m_Ctx.Fail(operands[i].value.Get(), QueryResult<void>::Fail(QueryError::NotAValue,
						"the asm block has two operands named '" + operands[i].name + "': name one of them with 'name = value'"));
		Plan plan;
		for (const AsmOperand& op : operands)
			if (auto added = AddOperand(op, plan); !added.Ok()) return added;
		if (options.pure && plan.locations.empty())
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "a 'pure' asm block needs an output: without one it does "
				"nothing and would be removed"));
		if (options.noReturn && !plan.locations.empty())
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "a 'noreturn' asm block never finishes, so it cannot have outputs"));

		std::vector<std::string> lines;
		for (const Core::String& line : n->GetLines()) lines.push_back(std::string(line.c_str()));
		auto text = AsmTemplate::Translate(lines, [&](const std::string& name) { return TemplateIndex(n, plan, name); });
		if (!text.Ok()) return m_Ctx.Fail(n, QueryResult<void>::Fail(text.error, text.message));
		auto clobbers = Clobbers(n, options);
		if (!clobbers.Ok()) return m_Ctx.Fail(n, QueryResult<void>::Fail(clobbers.error, clobbers.message));

		auto* node = m_Ctx.out->NewNode<AsmNode>();
		node->origin = n->startLocation;
		node->text = text.value;
		std::vector<std::string> constraints = plan.outputConstraints;
		constraints.insert(constraints.end(), plan.inputConstraints.begin(), plan.inputConstraints.end());
		node->constraints = Join(constraints) + (constraints.empty() || clobbers.value.empty() ? "" : ",") + clobbers.value;
		node->outputs = plan.slots;
		node->inputs = plan.inputs;
		node->memory = options.nomem ? AsmNode::Memory::None : options.readonly ? AsmNode::Memory::Read : AsmNode::Memory::Any;
		node->sideEffects = !options.pure;
		node->alignStack = !options.nostack;
		node->intel = !options.att;
		node->noReturn = options.noReturn;
		block->stmts.push_back(node);
		for (size_t i = 0; i < plan.locations.size(); ++i)
		{
			const LValue* slot = plan.slots[i];
			const Type* target = plan.locations[i]->type;
			const HIRNode* value = m_Ctx.out->NewNode<LoadNode>(slot->type, slot);
			if (TypeHandle::UnwrapToValue(target) != slot->type) value = m_Ctx.out->NewNode<ConvertNode>(value, slot->type, TypeHandle::UnwrapToValue(target));
			block->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(plan.locations[i], value));
		}
		return QueryResult<void>::Success();
	}

	QueryResult<void> AsmLowerer::AddOperand(const AsmOperand& op, Plan& plan)
	{
		if (op.kind == AsmOperand::Kind::Out || op.kind == AsmOperand::Kind::InOut) return AddOutput(op, plan);
		return AddInput(op, plan);
	}

	QueryResult<void> AsmLowerer::AddOutput(const AsmOperand& op, Plan& plan)
	{
		auto location = Location(op);
		if (!location.Ok()) return QueryResult<void>::Fail(location.error, location.message);
		auto reg = Classify(op, location.value->type);
		if (!reg.Ok()) return QueryResult<void>::Fail(reg.error, reg.message);
		auto letter = Letter(op, reg.value, location.value->type);
		if (!letter.Ok()) return QueryResult<void>::Fail(letter.error, letter.message);
		// An `out` register is not shared with an input (the block may write it before it reads them all); an
		// `inout` register is its own input.
		const bool tied = op.kind == AsmOperand::Kind::InOut;
		const int output = static_cast<int>(plan.locations.size());
		plan.locations.push_back(location.value);
		plan.slots.push_back(NewSlot(reg.value.type));
		plan.outputConstraints.push_back("=" + std::string(tied || op.explicitRegister ? "" : "&") + letter.value);
		plan.operandIndex.push_back(output);
		plan.inputOperand.push_back(false);
		if (tied)
		{
			plan.inputs.push_back(InRegister(m_Ctx.out->NewNode<LoadNode>(location.value->type, location.value), reg.value));
			plan.inputConstraints.push_back(std::to_string(output));
		}
		return QueryResult<void>::Success();
	}

	QueryResult<void> AsmLowerer::AddInput(const AsmOperand& op, Plan& plan)
	{
		const HIRNode* input = nullptr;
		std::string constraint = "i";
		if (op.kind == AsmOperand::Kind::Const)
		{
			auto constant = Constant(op);
			if (!constant.Ok()) return QueryResult<void>::Fail(constant.error, constant.message);
			input = constant.value;
		}
		else
		{
			auto value = ExprLowerer(m_Ctx).ResolveExpr(op.value.Get());
			if (!value.Ok()) return QueryResult<void>::Fail(value.error, value.message);
			const HIRNode* loaded = m_Ctx.LoadIfNeeded(value.value);
			auto reg = Classify(op, loaded->type);
			if (!reg.Ok()) return QueryResult<void>::Fail(reg.error, reg.message);
			auto letter = Letter(op, reg.value, loaded->type);
			if (!letter.Ok()) return QueryResult<void>::Fail(letter.error, letter.message);
			input = InRegister(loaded, reg.value);
			constraint = letter.value;
		}
		plan.operandIndex.push_back(static_cast<int>(plan.inputs.size()));
		plan.inputOperand.push_back(true);
		plan.inputs.push_back(input);
		plan.inputConstraints.push_back(constraint);
		return QueryResult<void>::Success();
	}

	QueryResult<AsmLowerer::RegisterValue> AsmLowerer::Classify(const AsmOperand& op, const Type* type)
	{
		using Result = QueryResult<RegisterValue>;
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* t = TypeHandle::UnwrapToValue(type);
		if (t && t->GetKind() == TypeKind::Bool) return Result(RegisterValue{ AsmRegisters::Kind::General, ts->Builtin(TypeKind::UInt8) });
		if (t && (t->IsIntegral() || t->GetKind() == TypeKind::Enum || t->GetKind() == TypeKind::Pointer || t->GetKind() == TypeKind::FunPtr))
			return Result(RegisterValue{ AsmRegisters::Kind::General, t });
		if (t && t->IsFloatingPoint()) return Result(RegisterValue{ AsmRegisters::Kind::Vector, t });
		if (const auto* lanes = dynamic_cast<const SwizzleType*>(t); lanes && lanes->IsPacked())
			return Result(RegisterValue{ AsmRegisters::Kind::Vector, t });
		if (const auto* object = dynamic_cast<const ObjectType*>(t); object && object->IsPurePackedVector())
		{
			const SwizzleType* lanes = object->GetPackedVectorSwizzle();
			return Result(RegisterValue{ AsmRegisters::Kind::Vector, ts->Swizzle(lanes->GetElementType(), lanes->GetLaneCount()) });
		}
		return m_Ctx.Fail(op.value.Get(), Result::Fail(QueryError::NotAValue, "the asm operand " + Described(op) + " is a '" +
			(t ? t->GetName() : Core::String("?")) + "': an operand is a number, a bool, an enum, a pointer or a packed vector"));
	}

	QueryResult<std::string> AsmLowerer::Letter(const AsmOperand& op, const RegisterValue& value, const Type* type)
	{
		using Result = QueryResult<std::string>;
		const std::string written(op.registerClass.c_str());
		const Core::String typeName = "'" + TypeHandle::UnwrapToValue(type)->GetName() + "'";
		auto fail = [&](const Core::String& why) { return m_Ctx.Fail(op.value.Get(), Result::Fail(QueryError::NotAValue, why)); };
		const bool general = value.kind == AsmRegisters::Kind::General;
		if (op.explicitRegister)
		{
			auto kind = AsmRegisters::KindOf(written);
			if (!kind) return fail("'" + op.registerClass + "' is not an x86-64 register");
			if (*kind != value.kind)
				return fail(general ? "the register '" + op.registerClass + "' holds floats and vectors, not a " + typeName + ": use a general register"
					: "the register '" + op.registerClass + "' holds integers and addresses, not a " + typeName + ": use an xmm register");
			return Result("{" + written + "}");
		}
		if (written == "reg")
		{
			if (general) return Result(std::string("r"));
			return fail("a " + typeName + " goes in an xmm register: write (xmm) instead of (reg)");
		}
		if (written == "xmm" || written == "ymm" || written == "zmm")
		{
			if (general) return fail("a " + typeName + " goes in a general register: write (reg) instead of (" + op.registerClass + ")");
			const size_t bytes = value.type->GetSize();
			if (bytes > ClassBytes(written))
				return fail("a " + typeName + " is " + std::to_string(bytes) + " bytes: a " + op.registerClass + " register holds " +
					std::to_string(ClassBytes(written)));
			return Result(std::string(written == "zmm" ? "v" : "x"));
		}
		return fail("'" + op.registerClass + "' is not an asm register class: use reg, xmm, ymm or zmm, or a register name in quotes");
	}

	QueryResult<const LValue*> AsmLowerer::Location(const AsmOperand& op)
	{
		using Result = QueryResult<const LValue*>;
		auto value = ExprLowerer(m_Ctx).ResolveExpr(op.value.Get());
		if (!value.Ok()) return Result::Fail(value.error, value.message);
		if (!value.value.isLValue)
			return m_Ctx.Fail(op.value.Get(), Result::Fail(QueryError::NotAValue, "the asm output " + Described(op) +
				" needs a location to store into, not a value"));
		Core::String why;
		if (ExprLowerer(m_Ctx).ChainBlocksWrite(value.value.lvalue, why)) return m_Ctx.Fail(op.value.Get(), Result::Fail(QueryError::NotAValue, why));
		return Result(value.value.lvalue);
	}

	const HIRNode* AsmLowerer::InRegister(const HIRNode* value, const RegisterValue& reg)
	{
		const Type* from = TypeHandle::UnwrapToValue(value->type);
		return from == reg.type ? value : m_Ctx.out->NewNode<ConvertNode>(value, from, reg.type);
	}

	QueryResult<const HIRNode*> AsmLowerer::Constant(const AsmOperand& op)
	{
		using Result = QueryResult<const HIRNode*>;
		ConstEvaluator eval(*m_Ctx.queries().Model(), m_Ctx.scope);
		auto value = eval.Evaluate(op.value.Get(), m_Ctx.unitId);
		if (!value.Ok() || (value.value.kind != ConstantValue::Kind::Int64 && value.value.kind != ConstantValue::Kind::Bool))
			return m_Ctx.Fail(op.value.Get(), Result::Fail(QueryError::NotAValue, "the asm constant '" + op.name +
				"' is a compile-time integer" + (value.Ok() ? Core::String() : ": " + value.message)));
		const Type* i64 = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
		return Result(m_Ctx.out->NewNode<ConstantNode>(i64, ConstantValue::Int(value.value.AsInt())));
	}

	QueryResult<std::string> AsmLowerer::Clobbers(const AsmStatementNode* n, const Options& options)
	{
		using Result = QueryResult<std::string>;
		std::vector<std::string> names = { "dirflag", "fpsr" };
		if (!options.preservesFlags) names.push_back("flags");
		const bool writesMemory = !options.nomem && !options.readonly;
		if (writesMemory) names.push_back("memory");
		// A caller-saved register an operand names is that operand (a call's result in rax), not a clobber.
		auto operandRegister = [&](const std::string& name) {
			for (const AsmOperand& op : n->GetOperands())
				if (op.explicitRegister && AsmRegisters::Family(std::string(op.registerClass.c_str())) == AsmRegisters::Family(name)) return true;
			return false;
		};
		if (n->ClobbersCallerSaved())
			for (const std::string& name : AsmRegisters::CallerSaved())
				if (!operandRegister(name)) names.push_back(name);
		for (const Core::String& written : n->GetClobbers())
		{
			const std::string name(written.c_str());
			if (name == "memory" && !writesMemory)
				return Result::Fail(QueryError::NotAValue, "the block is 'nomem' or 'readonly', so it cannot clobber \"memory\"");
			if ((name == "flags" || name == "cc") && options.preservesFlags)
				return Result::Fail(QueryError::NotAValue, "the block is 'preserves_flags', so it cannot clobber the flags");
			if (name == "memory" || name == "flags" || name == "cc") continue;
			if (!AsmRegisters::KindOf(name))
				return Result::Fail(QueryError::NotAValue, "'" + written + "' is not an x86-64 register: a clobber names a register, \"memory\" or \"flags\"");
			if (operandRegister(name))
				return Result::Fail(QueryError::NotAValue, "'" + written + "' is an operand's register in this block, so it is not a clobber: "
					"the operand already says the block uses it");
			names.push_back(name);
		}
		std::vector<std::string> unique;
		for (const std::string& name : names)
			if (std::find(unique.begin(), unique.end(), name) == unique.end()) unique.push_back(name);
		std::vector<std::string> parts;
		for (const std::string& name : unique) parts.push_back("~{" + name + "}");
		return Result(Join(parts));
	}

	int AsmLowerer::TemplateIndex(const AsmStatementNode* n, const Plan& plan, const std::string& name) const
	{
		const auto& operands = n->GetOperands();
		size_t position = static_cast<size_t>(operands.Count());
		if (IsNumber(name)) position = static_cast<size_t>(std::stoul(name));
		else
			for (size_t i = 0; i < static_cast<size_t>(operands.Count()); ++i)
				if (std::string(operands[i].name.c_str()) == name) position = i;
		if (position >= static_cast<size_t>(operands.Count())) return -1;
		return plan.operandIndex[position] + (plan.inputOperand[position] ? static_cast<int>(plan.locations.size()) : 0);
	}

	const LValue* AsmLowerer::NewSlot(const Type* type)
	{
		LValue lv;
		lv.kind = LValue::Kind::Slot;
		lv.type = type;
		lv.slot = m_Ctx.AllocSlot(type, "__asm_output", SlotClass::Temp);
		return m_Ctx.NewLValue(lv);
	}
}
