#include "Compiler/StackMapRecords.h"
#include "Compiler/StackMapValues.h"
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/ValueHandle.h>
#include <iterator>

namespace C4Compiler
{
	namespace
	{
		constexpr unsigned c_AddressBits = 64;

		// The words of `value` that may hold an address, each a value a stack map records as it is: a pointer or a 64-bit
		// integer. Made right before the call from a vector, an aggregate or a wider integer.
		void AddressWords(llvm::IRBuilder<>& B, llvm::Value* value, std::vector<llvm::Value*>& words)
		{
			llvm::Type* type = value->getType();
			if (type->isPointerTy() || type->isIntegerTy(c_AddressBits))
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

			// `value`'s slot; null when it is no word or cannot be stored where it is made.
			llvm::AllocaInst* Of(llvm::Value* value)
			{
				const auto found = m_Slots.find(value);
				if (found != m_Slots.end()) return found->second;
				llvm::AllocaInst* slot = nullptr;
				llvm::Instruction* at = AfterDefinition(value);
				if (at && (value->getType()->isPointerTy() || value->getType()->isIntegerTy(c_AddressBits)))
				{
					llvm::BasicBlock& entry = m_Function.getEntryBlock();
					slot = new llvm::AllocaInst(value->getType(), m_Function.getDataLayout().getAllocaAddrSpace(), "stack_map.value", entry.begin());
					new llvm::StoreInst(value, slot, at->getIterator());
				}
				m_Slots.emplace(value, slot);
				return slot;
			}

		private:
			llvm::Function& m_Function;
			std::unordered_map<const llvm::Value*, llvm::AllocaInst*> m_Slots;
		};

		// A call's record before it is listed: the values recorded as they are (followed through the calls replaced as they
		// get their lists), and the slots of those recorded through one.
		struct Record
		{
			std::vector<llvm::WeakTrackingVH> values;
			std::vector<llvm::AllocaInst*> valueSlots;
		};

		std::unordered_map<llvm::CallBase*, Record> Records(llvm::Function& function, const std::vector<llvm::CallBase*>& calls, const CallLists& values)
		{
			std::unordered_map<llvm::CallBase*, Record> records;
			if (values.empty()) return records;
			const llvm::DominatorTree tree(function);
			const llvm::LoopInfo loops(tree);
			std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>> quiet;
			for (llvm::CallBase* call : calls)
			{
				const auto live = values.find(call);
				if (live == values.end()) continue;
				const llvm::Loop* loop = loops.getLoopFor(call->getParent());
				for (llvm::Value* value : live->second)
				{
					if (loop && !UsedIn(value, *loop)) quiet[call].push_back(value);
					else records[call].values.emplace_back(value);
				}
			}
			ValueSlots slots(function);
			for (const auto& [call, list] : quiet)
				for (llvm::Value* value : list)
				{
					if (llvm::AllocaInst* slot = slots.Of(value)) records[call].valueSlots.push_back(slot);
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
			list.reserve(1 + words.size() + 3 * record.valueSlots.size() + frameSlots.size());
			list.push_back(B.getInt32(static_cast<uint32_t>(words.size())));
			list.insert(list.end(), words.begin(), words.end());
			for (llvm::AllocaInst* slot : record.valueSlots)
			{
				list.push_back(slot);
				list.push_back(B.getInt32(1)); // one word
				list.push_back(B.getInt32(1)); // which may hold a pointer
			}
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

	void RecordCalls(llvm::Function& function, const std::vector<llvm::CallBase*>& calls, const CallLists& values, const CallLists& slots)
	{
		const std::unordered_map<llvm::CallBase*, Record> records = Records(function, calls, values);
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
