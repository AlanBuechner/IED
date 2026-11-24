#include "Compiler/StackMapRecords.h"
#include "Compiler/StackMapSlots.h"
#include "Compiler/StackMapValues.h"
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/ValueHandle.h>
#include <algorithm>
#include <iterator>
#include <optional>

namespace C4Compiler
{
	namespace
	{
		constexpr unsigned c_AddressBits = 64;
		// The stack's own alignment: a slot aligned more would make its frame realign the stack.
		constexpr uint64_t c_StackAlignBytes = 16;

		// A value a stack map records as it is: a pointer or a 64-bit integer.
		bool IsWord(const llvm::Type* type) { return type->isPointerTy() || type->isIntegerTy(c_AddressBits); }

		// The words of `value` that may hold an address. Made right before the call from a vector, an aggregate or a wider
		// integer.
		void AddressWords(llvm::IRBuilder<>& B, llvm::Value* value, std::vector<llvm::Value*>& words)
		{
			llvm::Type* type = value->getType();
			if (IsWord(type))
			{
				words.push_back(value);
				return;
			}
			if (type->isIntegerTy())
			{
				for (unsigned at = 0; at < type->getIntegerBitWidth(); at += c_AddressBits)
					words.push_back(B.CreateTrunc(at ? B.CreateLShr(value, at) : value, B.getInt64Ty()));
				return;
			}
			if (auto* vector = llvm::dyn_cast<llvm::FixedVectorType>(type))
			{
				for (unsigned lane = 0; lane < vector->getNumElements(); ++lane) AddressWords(B, B.CreateExtractElement(value, lane), words);
				return;
			}
			if (auto* structure = llvm::dyn_cast<llvm::StructType>(type))
			{
				for (unsigned i = 0; i < structure->getNumElements(); ++i)
					if (MayHoldAddress(structure->getElementType(i))) AddressWords(B, B.CreateExtractValue(value, i), words);
				return;
			}
			if (auto* array = llvm::dyn_cast<llvm::ArrayType>(type))
				for (unsigned i = 0; i < array->getNumElements(); ++i) AddressWords(B, B.CreateExtractValue(value, i), words);
		}

		// Whether `loop` makes or reads `value` (a phi reads it at the end of the block it comes from).
		bool UsedIn(const llvm::Value* value, const llvm::Loop& loop)
		{
			if (const auto* made = llvm::dyn_cast<llvm::Instruction>(value); made && loop.contains(made)) return true;
			for (const llvm::Use& use : value->uses())
			{
				const auto* user = llvm::dyn_cast<llvm::Instruction>(use.getUser());
				if (!user) continue;
				const auto* phi = llvm::dyn_cast<llvm::PHINode>(user);
				if (loop.contains(phi ? phi->getIncomingBlock(use) : user->getParent())) return true;
			}
			return false;
		}

		// Where a store of `value` may first go: right after it, after its block's phis, at the start of an invoke's normal
		// successor, at the start of the entry for an argument; null where there is no such place (a normal successor with
		// other ways in, a block an exception dispatch begins).
		llvm::Instruction* AfterDefinition(llvm::Value* value)
		{
			if (auto* argument = llvm::dyn_cast<llvm::Argument>(value)) return &*argument->getParent()->getEntryBlock().getFirstInsertionPt();
			auto* instruction = llvm::cast<llvm::Instruction>(value);
			llvm::BasicBlock* block = instruction->getParent();
			llvm::BasicBlock::iterator at = std::next(instruction->getIterator());
			if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(instruction))
			{
				block = invoke->getNormalDest();
				if (!block->getSinglePredecessor()) return nullptr;
				at = block->getFirstInsertionPt();
			}
			else if (llvm::isa<llvm::PHINode>(instruction)) at = block->getFirstInsertionPt();
			return at == block->end() ? nullptr : &*at;
		}

		// The slots that hold values recorded through them, each made once: stored right where its value is made.
		class ValueSlots
		{
		public:
			explicit ValueSlots(llvm::Function& function) : m_Function(function) {}

			// `value`'s slot as a record lists it (StackMapSlots: the slot, its words, its masks); none when it cannot be
			// stored where it is made.
			const std::vector<llvm::Value*>& Of(llvm::Value* value)
			{
				const auto found = m_Slots.find(value);
				if (found != m_Slots.end()) return found->second;
				std::vector<llvm::Value*> operands;
				if (llvm::Instruction* at = AfterDefinition(value)) operands = Made(value, *at);
				return m_Slots.emplace(value, std::move(operands)).first->second;
			}

		private:
			std::vector<llvm::Value*> Made(llvm::Value* value, llvm::Instruction& at)
			{
				llvm::Type* type = value->getType();
				const llvm::DataLayout& layout = m_Function.getDataLayout();
				const llvm::Align align = std::min(layout.getPrefTypeAlign(type), llvm::Align(c_StackAlignBytes));
				auto* slot = new llvm::AllocaInst(type, layout.getAllocaAddrSpace(), nullptr, align, "stack_map.value", m_Function.getEntryBlock().begin());
				std::vector<llvm::Value*> operands = SlotOperands(*slot);
				if (operands.empty()) slot->eraseFromParent();
				else new llvm::StoreInst(value, slot, false, align, at.getIterator());
				return operands;
			}

			llvm::Function& m_Function;
			std::unordered_map<const llvm::Value*, std::vector<llvm::Value*>> m_Slots;
		};

		// A call's record before it is listed: the values recorded as they are (followed through the calls replaced as they
		// get their lists), and the slots of those recorded through one, as listed.
		struct Record
		{
			std::vector<llvm::WeakTrackingVH> values;
			std::vector<llvm::Value*> valueSlots;
		};

		// Which of the values live across a call a record names through a slot (see RecordCalls). In place, only a word
		// goes through one: a slot for an aggregate or a wider value too made the Bedrock benchmark's code 0.6% larger.
		class SlotChoice
		{
		public:
			SlotChoice(llvm::Function& function, RecordedValues where)
			{
				if (where == RecordedValues::InSlots) return;
				m_Tree.emplace(function);
				m_Loops.emplace(*m_Tree);
			}

			bool ThroughSlot(const llvm::Value* value, const llvm::CallBase& call) const
			{
				if (!m_Loops) return true;
				const llvm::Loop* loop = m_Loops->getLoopFor(call.getParent());
				return loop && IsWord(value->getType()) && !UsedIn(value, *loop);
			}

		private:
			std::optional<llvm::DominatorTree> m_Tree;
			std::optional<llvm::LoopInfo> m_Loops;
		};

		std::unordered_map<llvm::CallBase*, Record> Records(llvm::Function& function, const std::vector<llvm::CallBase*>& calls, const CallLists& values,
			RecordedValues where)
		{
			std::unordered_map<llvm::CallBase*, Record> records;
			if (values.empty()) return records;
			const SlotChoice choice(function, where);
			// In the calls' order, so the slots are made in the same order every time.
			std::vector<std::pair<llvm::CallBase*, llvm::Value*>> slotted;
			for (llvm::CallBase* call : calls)
			{
				const auto live = values.find(call);
				if (live == values.end()) continue;
				for (llvm::Value* value : live->second)
				{
					if (choice.ThroughSlot(value, *call)) slotted.emplace_back(call, value);
					else records[call].values.emplace_back(value);
				}
			}
			ValueSlots slots(function);
			for (const auto& [call, value] : slotted)
			{
				const std::vector<llvm::Value*>& slot = slots.Of(value);
				if (!slot.empty()) records[call].valueSlots.insert(records[call].valueSlots.end(), slot.begin(), slot.end());
				else records[call].values.emplace_back(value);
			}
			return records;
		}

		// The call's deopt operands: the count of words of live values, those words, then slots with their words and masks.
		std::vector<llvm::Value*> DeoptList(llvm::CallBase* call, const Record& record, const std::vector<llvm::Value*>& frameSlots)
		{
			llvm::IRBuilder<> B(call);
			std::vector<llvm::Value*> words;
			for (llvm::Value* value : record.values) AddressWords(B, value, words);
			std::vector<llvm::Value*> list;
			list.reserve(1 + words.size() + record.valueSlots.size() + frameSlots.size());
			list.push_back(B.getInt32(static_cast<uint32_t>(words.size())));
			list.insert(list.end(), words.begin(), words.end());
			list.insert(list.end(), record.valueSlots.begin(), record.valueSlots.end());
			list.insert(list.end(), frameSlots.begin(), frameSlots.end());
			return list;
		}

		void ListDeopt(llvm::CallBase* call, const std::vector<llvm::Value*>& list)
		{
			llvm::CallBase* listed = llvm::CallBase::addOperandBundle(call, llvm::LLVMContext::OB_deopt, llvm::OperandBundleDef("deopt", list),
				call->getIterator());
			listed->takeName(call);
			call->replaceAllUsesWith(listed);
			call->eraseFromParent();
		}
	}

	void RecordCalls(llvm::Function& function, const std::vector<llvm::CallBase*>& calls, const CallLists& values, const CallLists& slots,
		RecordedValues where)
	{
		const std::unordered_map<llvm::CallBase*, Record> records = Records(function, calls, values, where);
		const Record none;
		const std::vector<llvm::Value*> noSlots;
		for (llvm::CallBase* call : calls)
		{
			const auto record = records.find(call);
			const auto frameSlots = slots.find(call);
			ListDeopt(call, DeoptList(call, record == records.end() ? none : record->second, frameSlots == slots.end() ? noSlots : frameSlots->second));
		}
	}
}
