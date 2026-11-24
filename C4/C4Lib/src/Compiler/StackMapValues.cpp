#include "Compiler/StackMapValues.h"
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_set>

namespace C4Compiler
{
	namespace
	{
		constexpr unsigned c_AddressBits = 64;

		bool IsWideInteger(const llvm::Type* type) { return type->isIntegerTy() && type->getIntegerBitWidth() >= c_AddressBits; }

		// An integer computed from others that holds an address only when one of them does: an address moved by an offset,
		// aligned, tagged, or chosen. A product, a quotient or a shift of one is no address.
		bool PassesAddressOn(const llvm::Instruction& instruction)
		{
			switch (instruction.getOpcode())
			{
			case llvm::Instruction::Add: case llvm::Instruction::Sub: case llvm::Instruction::And: case llvm::Instruction::Or:
			case llvm::Instruction::Xor: case llvm::Instruction::PHI: case llvm::Instruction::Select: case llvm::Instruction::Freeze:
				return true;
			default:
				return false;
			}
		}

		// An integer that may be an address the optimizer carries as one (a copy of a structure holding a pointer): an
		// argument, a load, a call's result, `ptrtoint`, a part of an aggregate or a vector, an atomic's old value, the low part
		// of a wider integer. A product, a count, a float converted, a narrower integer widened is none.
		bool MayStartAddress(const llvm::Value& value)
		{
			if (llvm::isa<llvm::Argument>(value)) return true;
			const auto* instruction = llvm::dyn_cast<llvm::Instruction>(&value);
			if (!instruction) return false;
			if (const auto* call = llvm::dyn_cast<llvm::CallBase>(instruction)) return !call->getCalledFunction() || !call->getCalledFunction()->isIntrinsic();
			if (const auto* trunc = llvm::dyn_cast<llvm::TruncInst>(instruction)) return trunc->getSrcTy()->getScalarSizeInBits() > c_AddressBits;
			return llvm::isa<llvm::LoadInst>(instruction) || llvm::isa<llvm::PtrToIntInst>(instruction) || llvm::isa<llvm::ExtractValueInst>(instruction) ||
				llvm::isa<llvm::ExtractElementInst>(instruction) || llvm::isa<llvm::AtomicRMWInst>(instruction) || llvm::isa<llvm::VAArgInst>(instruction);
		}

		// The wide integers of `function` that may hold an address: those that may be one (MayStartAddress), and from them
		// every one they pass on to (PassesAddressOn).
		std::unordered_set<const llvm::Value*> IntegersHoldingAddresses(llvm::Function& function)
		{
			std::unordered_set<const llvm::Value*> holding;
			std::vector<const llvm::Value*> pending;
			auto seed = [&](const llvm::Value& value) {
				if (IsWideInteger(value.getType()) && MayStartAddress(value) && holding.insert(&value).second) pending.push_back(&value);
			};
			for (const llvm::Argument& argument : function.args()) seed(argument);
			for (const llvm::BasicBlock& block : function)
				for (const llvm::Instruction& instruction : block) seed(instruction);
			while (!pending.empty())
			{
				const llvm::Value* value = pending.back();
				pending.pop_back();
				for (const llvm::User* user : value->users())
					if (const auto* instruction = llvm::dyn_cast<llvm::Instruction>(user))
						if (IsWideInteger(instruction->getType()) && PassesAddressOn(*instruction) && holding.insert(instruction).second) pending.push_back(instruction);
			}
			return holding;
		}

		// Whether `value` may name a heap object where a call returns (see ValuesLiveAcross).
		bool MayNameObject(const llvm::Value* value, const std::unordered_set<const llvm::Value*>& integers)
		{
			if (!llvm::isa<llvm::Instruction>(value) && !llvm::isa<llvm::Argument>(value)) return false;
			if (!MayHoldAddress(value->getType())) return false;
			if (IsWideInteger(value->getType())) return integers.count(value) != 0;
			if (value->getType()->isPointerTy())
			{
				const llvm::Value* object = llvm::getUnderlyingObject(value);
				return !llvm::isa<llvm::AllocaInst>(object) && !llvm::isa<llvm::GlobalValue>(object);
			}
			return true;
		}

		// What `value` points into or is offset from: a pointer's underlying object, an integer's operand where a constant
		// is added, masked or mixed in; else `value` itself.
		const llvm::Value* BaseOf(const llvm::Value* value)
		{
			if (value->getType()->isPointerTy()) return llvm::getUnderlyingObject(value);
			const auto* operation = llvm::dyn_cast<llvm::BinaryOperator>(value);
			if (!operation || !PassesAddressOn(*operation)) return value;
			if (llvm::isa<llvm::Constant>(operation->getOperand(1))) return BaseOf(operation->getOperand(0));
			if (llvm::isa<llvm::Constant>(operation->getOperand(0))) return BaseOf(operation->getOperand(1));
			return value;
		}

		// The function's values that may name an object, each kept by its base: the bases (values whose base is themselves or
		// names no object) numbered in the order the function defines them, and for each base the values it keeps, itself
		// first. The collector keeps an object for any address inside it, so where an address made from a base (a field's, an
		// offset integer) is read after a call, the base is what the record names: code generation keeps the base across the
		// call and makes the field's address again after it, and would otherwise want a register for each at the call.
		struct Candidates
		{
			std::vector<llvm::Value*> values;
			std::vector<std::vector<const llvm::Value*>> kept;
			std::unordered_map<const llvm::Value*, uint32_t> keptBy;

			std::optional<uint32_t> KeeperOf(const llvm::Value* value) const
			{
				const auto found = keptBy.find(value);
				return found == keptBy.end() ? std::nullopt : std::optional<uint32_t>(found->second);
			}
		};

		Candidates CandidatesOf(llvm::Function& function)
		{
			const std::unordered_set<const llvm::Value*> integers = IntegersHoldingAddresses(function);
			std::vector<llvm::Value*> naming;
			for (llvm::Argument& argument : function.args())
				if (MayNameObject(&argument, integers)) naming.push_back(&argument);
			for (llvm::BasicBlock& block : function)
				for (llvm::Instruction& instruction : block)
					if (MayNameObject(&instruction, integers)) naming.push_back(&instruction);
			const std::unordered_set<const llvm::Value*> named(naming.begin(), naming.end());
			auto keeperOf = [&](const llvm::Value* value) {
				const llvm::Value* base = BaseOf(value);
				return named.count(base) ? base : value;
			};
			Candidates candidates;
			std::unordered_map<const llvm::Value*, uint32_t> number;
			for (llvm::Value* value : naming)
				if (keeperOf(value) == value)
				{
					number.emplace(value, static_cast<uint32_t>(candidates.values.size()));
					candidates.values.push_back(value);
					candidates.kept.push_back({ value });
				}
			for (llvm::Value* value : naming)
			{
				const uint32_t keeper = number.at(keeperOf(value));
				candidates.keptBy.emplace(value, keeper);
				if (candidates.values[keeper] != value) candidates.kept[keeper].push_back(value);
			}
			return candidates;
		}

		const llvm::BasicBlock* Home(const llvm::Value* value)
		{
			if (const auto* argument = llvm::dyn_cast<llvm::Argument>(value)) return &argument->getParent()->getEntryBlock();
			return llvm::cast<llvm::Instruction>(value)->getParent();
		}

		// For each block (by number), the bases live where it ends: the uses of each base and of every value it keeps are
		// followed back to the base's definition (a phi's use is at the end of the block it comes from).
		std::vector<std::vector<uint32_t>> LiveOut(const llvm::Function& function, const Candidates& candidates)
		{
			const unsigned blocks = function.getMaxBlockNumber();
			std::vector<std::vector<uint32_t>> liveOut(blocks);
			std::vector<uint32_t> atStart(blocks, 0), atEnd(blocks, 0);
			std::vector<const llvm::BasicBlock*> pending;
			for (uint32_t i = 0; i < candidates.values.size(); ++i)
			{
				const llvm::Value* value = candidates.values[i];
				const uint32_t stamp = i + 1;
				const llvm::BasicBlock* home = Home(value);
				auto liveAtEnd = [&](const llvm::BasicBlock* block) {
					if (atEnd[block->getNumber()] == stamp) return;
					atEnd[block->getNumber()] = stamp;
					liveOut[block->getNumber()].push_back(i);
				};
				auto liveAtStart = [&](const llvm::BasicBlock* block) {
					if (block == home || atStart[block->getNumber()] == stamp) return;
					atStart[block->getNumber()] = stamp;
					pending.push_back(block);
				};
				for (const llvm::Value* kept : candidates.kept[i])
					for (const llvm::Use& use : kept->uses())
					{
						const auto* user = llvm::dyn_cast<llvm::Instruction>(use.getUser());
						if (!user) continue;
						if (const auto* phi = llvm::dyn_cast<llvm::PHINode>(user))
						{
							liveAtEnd(phi->getIncomingBlock(use));
							liveAtStart(phi->getIncomingBlock(use));
						}
						else liveAtStart(user->getParent());
					}
				while (!pending.empty())
				{
					const llvm::BasicBlock* block = pending.back();
					pending.pop_back();
					for (const llvm::BasicBlock* before : llvm::predecessors(block))
					{
						liveAtEnd(before);
						liveAtStart(before);
					}
				}
			}
			return liveOut;
		}

		// The candidates live at a point of a block walked backward.
		class LiveSet
		{
		public:
			explicit LiveSet(size_t count) : m_At(count, c_Absent) {}

			void Add(uint32_t value)
			{
				if (m_At[value] != c_Absent) return;
				m_At[value] = static_cast<uint32_t>(m_List.size());
				m_List.push_back(value);
			}

			void Remove(uint32_t value)
			{
				const uint32_t at = m_At[value];
				if (at == c_Absent) return;
				m_List[at] = m_List.back();
				m_At[m_List[at]] = at;
				m_List.pop_back();
				m_At[value] = c_Absent;
			}

			void Clear()
			{
				for (uint32_t value : m_List) m_At[value] = c_Absent;
				m_List.clear();
			}

			// The live bases in the order the function defines them.
			std::vector<uint32_t> Sorted() const
			{
				std::vector<uint32_t> sorted = m_List;
				std::sort(sorted.begin(), sorted.end());
				return sorted;
			}

		private:
			static constexpr uint32_t c_Absent = UINT32_MAX;
			std::vector<uint32_t> m_At;
			std::vector<uint32_t> m_List;
		};
	}

	bool MayHoldAddress(const llvm::Type* type)
	{
		if (type->isPointerTy()) return true;
		if (type->isIntegerTy()) return type->getIntegerBitWidth() >= c_AddressBits;
		if (const auto* vector = llvm::dyn_cast<llvm::VectorType>(type)) return MayHoldAddress(vector->getElementType());
		if (const auto* array = llvm::dyn_cast<llvm::ArrayType>(type)) return MayHoldAddress(array->getElementType());
		if (const auto* structure = llvm::dyn_cast<llvm::StructType>(type))
			return std::any_of(structure->element_begin(), structure->element_end(), [](const llvm::Type* element) { return MayHoldAddress(element); });
		return false;
	}

	std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>> ValuesLiveAcross(llvm::Function& function, const std::vector<llvm::CallBase*>& calls)
	{
		std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>> listed;
		if (calls.empty()) return listed;
		const Candidates candidates = CandidatesOf(function);
		if (candidates.values.empty()) return listed;
		const std::unordered_set<const llvm::CallBase*> wanted(calls.begin(), calls.end());
		std::unordered_set<const llvm::BasicBlock*> withCalls;
		for (const llvm::CallBase* call : calls) withCalls.insert(call->getParent());
		const std::vector<std::vector<uint32_t>> liveOut = LiveOut(function, candidates);
		LiveSet live(candidates.values.size());
		for (llvm::BasicBlock& block : function)
		{
			if (!withCalls.count(&block)) continue;
			live.Clear();
			for (uint32_t value : liveOut[block.getNumber()]) live.Add(value);
			for (llvm::Instruction& instruction : llvm::reverse(block))
			{
				if (const std::optional<uint32_t> self = candidates.KeeperOf(&instruction); self && candidates.values[*self] == &instruction) live.Remove(*self);
				if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction); call && wanted.count(call))
				{
					std::vector<llvm::Value*>& values = listed[call];
					for (uint32_t value : live.Sorted()) values.push_back(candidates.values[value]);
				}
				if (llvm::isa<llvm::PHINode>(instruction)) continue;
				for (const llvm::Use& operand : instruction.operands())
					if (const std::optional<uint32_t> keeper = candidates.KeeperOf(operand.get())) live.Add(*keeper);
			}
		}
		return listed;
	}
}
