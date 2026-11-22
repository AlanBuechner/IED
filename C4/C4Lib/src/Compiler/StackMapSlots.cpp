#include "Compiler/StackMapSlots.h"
#include "Compiler/StackMaps.h"
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <algorithm>
#include <cstdint>
#include <optional>

namespace C4Compiler
{
	namespace
	{
		// A frame slot of more words is read whole: its map would cost a constant per 32 words at every call.
		constexpr uint64_t c_MappedWordsMost = 1024;
		constexpr uint64_t c_WordBytes = 8;
		constexpr uint64_t c_MaskWords = 32;

		bool IsManaged(const llvm::Type* type)
		{
			const auto* pointer = llvm::dyn_cast<llvm::PointerType>(type->getScalarType());
			return pointer && pointer->getAddressSpace() == c_ManagedAddressSpace;
		}

		// Whether a word of a `type` may hold a managed pointer: a managed pointer's, any of an array of bytes (a union,
		// storage a copy went through) and any of an integer as wide as an address (a part the optimizer typed so). A
		// float, a narrower integer or a raw pointer holds none.
		bool MayHoldAny(const llvm::Type* type)
		{
			if (type->isPointerTy() || type->isVectorTy()) return IsManaged(type);
			if (type->isIntegerTy()) return type->getIntegerBitWidth() >= 64;
			if (const auto* structure = llvm::dyn_cast<llvm::StructType>(type))
			{
				for (const llvm::Type* element : structure->elements())
					if (MayHoldAny(element)) return true;
				return false;
			}
			if (const auto* array = llvm::dyn_cast<llvm::ArrayType>(type))
				return array->getElementType()->isIntegerTy(8) ? array->getNumElements() >= c_WordBytes : MayHoldAny(array->getElementType());
			return false;
		}

		void MarkWords(const llvm::DataLayout& layout, llvm::Type* type, uint64_t offset, std::vector<bool>& words)
		{
			if (!MayHoldAny(type)) return;
			auto mark = [&](uint64_t from, uint64_t to) {
				for (uint64_t word = (from + c_WordBytes - 1) / c_WordBytes; (word + 1) * c_WordBytes <= to && word < words.size(); ++word) words[word] = true;
			};
			if (type->isPointerTy() || type->isIntegerTy() || type->isVectorTy())
			{
				mark(offset, offset + layout.getTypeStoreSize(type));
				return;
			}
			if (auto* structure = llvm::dyn_cast<llvm::StructType>(type))
			{
				const llvm::StructLayout* places = layout.getStructLayout(structure);
				for (unsigned i = 0; i < structure->getNumElements(); ++i)
					MarkWords(layout, structure->getElementType(i), offset + places->getElementOffset(i), words);
				return;
			}
			if (auto* array = llvm::dyn_cast<llvm::ArrayType>(type))
			{
				llvm::Type* element = array->getElementType();
				if (element->isIntegerTy(8))
				{
					mark(offset, offset + array->getNumElements());
					return;
				}
				const uint64_t size = layout.getTypeAllocSize(element);
				for (uint64_t i = 0; i < array->getNumElements(); ++i) MarkWords(layout, element, offset + i * size, words);
			}
		}

		// A slot's deopt operands (see FrameSlotsAt); none when no word of it may hold a managed pointer.
		std::vector<llvm::Value*> Operands(llvm::AllocaInst& slot, uint64_t bytes)
		{
			const llvm::DataLayout& layout = slot.getDataLayout();
			const uint64_t count = bytes / c_WordBytes;
			std::vector<bool> words(count, false);
			const uint64_t elementSize = layout.getTypeAllocSize(slot.getAllocatedType());
			for (uint64_t at = 0; elementSize && at + elementSize <= bytes; at += elementSize) MarkWords(layout, slot.getAllocatedType(), at, words);
			if (std::find(words.begin(), words.end(), true) == words.end()) return {};
			llvm::Type* i32 = llvm::Type::getInt32Ty(slot.getContext());
			std::vector<llvm::Value*> operands{ &slot };
			if (count > c_MappedWordsMost)
			{
				operands.push_back(llvm::ConstantInt::get(i32, static_cast<uint32_t>(count) | c_WholeSlot));
				return operands;
			}
			operands.push_back(llvm::ConstantInt::get(i32, count));
			for (uint64_t first = 0; first < count; first += c_MaskWords)
			{
				uint32_t mask = 0;
				for (uint64_t w = first; w < count && w < first + c_MaskWords; ++w) mask |= words[w] ? 1u << (w - first) : 0u;
				operands.push_back(llvm::ConstantInt::get(i32, mask));
			}
			return operands;
		}

		// --- which slots are reached only by their own loads and stores -----------------------------------------------

		// A frame slot and the addresses made from it (its fields' GEPs).
		struct Slot
		{
			llvm::AllocaInst* alloca = nullptr;
			uint64_t bytes = 0;
			std::vector<llvm::Value*> operands;
			bool escapes = false;
		};

		// Whether `slot`'s address goes anywhere but into its own loads, stores, copies and lifetime markers; else every
		// address made from it is noted in `of` as the slot's.
		bool Escapes(llvm::AllocaInst* slot, size_t index, std::unordered_map<const llvm::Value*, size_t>& of)
		{
			std::vector<const llvm::Value*> pending{ slot };
			std::vector<const llvm::Value*> made;
			while (!pending.empty())
			{
				const llvm::Value* address = pending.back();
				pending.pop_back();
				made.push_back(address);
				for (const llvm::Use& use : address->uses())
				{
					const auto* user = llvm::dyn_cast<llvm::Instruction>(use.getUser());
					if (!user) return true;
					if (llvm::isa<llvm::LoadInst>(user)) continue;
					if (const auto* store = llvm::dyn_cast<llvm::StoreInst>(user))
					{
						if (store->getValueOperand() == address) return true;
						continue;
					}
					if (llvm::isa<llvm::GetElementPtrInst>(user))
					{
						pending.push_back(user);
						continue;
					}
					if (const auto* intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(user))
						if (intrinsic->isLifetimeStartOrEnd() || llvm::isa<llvm::MemIntrinsic>(intrinsic)) continue;
					return true;
				}
			}
			for (const llvm::Value* address : made) of[address] = index;
			return false;
		}

		// What an instruction does to the slots it touches: reads part of one, or writes all of one.
		struct Touch
		{
			size_t slot;
			bool reads;
		};

		std::optional<size_t> SlotOf(const llvm::Value* address, const std::unordered_map<const llvm::Value*, size_t>& of)
		{
			const auto found = of.find(address);
			return found == of.end() ? std::nullopt : std::optional<size_t>(found->second);
		}

		// Whether a write at `address` of `bytes` covers the whole slot it is in.
		bool Covers(const llvm::Value* address, uint64_t bytes, const Slot& slot) { return address == slot.alloca && bytes >= slot.bytes; }

		void TouchesOf(const llvm::Instruction& instruction, const std::vector<Slot>& slots, const std::unordered_map<const llvm::Value*, size_t>& of,
			std::vector<Touch>& touches)
		{
			touches.clear();
			const llvm::DataLayout& layout = instruction.getDataLayout();
			if (const auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction))
			{
				if (const auto slot = SlotOf(load->getPointerOperand(), of)) touches.push_back({ *slot, true });
				return;
			}
			if (const auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction))
			{
				const auto slot = SlotOf(store->getPointerOperand(), of);
				if (slot && Covers(store->getPointerOperand(), layout.getTypeStoreSize(store->getValueOperand()->getType()), slots[*slot]))
					touches.push_back({ *slot, false });
				return;
			}
			const auto* intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(&instruction);
			if (!intrinsic) return;
			if (intrinsic->isLifetimeStartOrEnd())
			{
				// Before its lifetime starts and after it ends a slot holds nothing.
				if (const auto slot = SlotOf(intrinsic->getArgOperand(intrinsic->arg_size() - 1), of)) touches.push_back({ *slot, false });
				return;
			}
			if (const auto* transfer = llvm::dyn_cast<llvm::MemTransferInst>(intrinsic))
				if (const auto slot = SlotOf(transfer->getRawSource(), of)) touches.push_back({ *slot, true });
			if (const auto* memory = llvm::dyn_cast<llvm::MemIntrinsic>(intrinsic))
			{
				const auto slot = SlotOf(memory->getRawDest(), of);
				const auto* length = llvm::dyn_cast<llvm::ConstantInt>(memory->getLength());
				if (slot && length && Covers(memory->getRawDest(), length->getZExtValue(), slots[*slot])) touches.push_back({ *slot, false });
			}
		}

		// --- liveness ------------------------------------------------------------------------------------------------

		class SlotSet
		{
		public:
			explicit SlotSet(size_t count = 0) : m_Words((count + 63) / 64, 0) {}
			void Set(size_t i) { m_Words[i / 64] |= uint64_t(1) << (i % 64); }
			void Reset(size_t i) { m_Words[i / 64] &= ~(uint64_t(1) << (i % 64)); }
			bool Has(size_t i) const { return (m_Words[i / 64] >> (i % 64)) & 1u; }
			bool Join(const SlotSet& other)
			{
				bool grew = false;
				for (size_t w = 0; w < m_Words.size(); ++w)
				{
					const uint64_t joined = m_Words[w] | other.m_Words[w];
					grew |= joined != m_Words[w];
					m_Words[w] = joined;
				}
				return grew;
			}
			bool operator==(const SlotSet& other) const { return m_Words == other.m_Words; }

		private:
			std::vector<uint64_t> m_Words;
		};

		// The slots live where each block ends: those a later load reads before a store over all of them.
		std::unordered_map<const llvm::BasicBlock*, SlotSet> LiveOut(llvm::Function& function, const std::vector<Slot>& slots,
			const std::unordered_map<const llvm::Value*, size_t>& of)
		{
			std::unordered_map<const llvm::BasicBlock*, SlotSet> in, out;
			std::vector<Touch> touches;
			for (llvm::BasicBlock& block : function)
			{
				in.emplace(&block, SlotSet(slots.size()));
				out.emplace(&block, SlotSet(slots.size()));
			}
			for (bool changed = true; changed; )
			{
				changed = false;
				for (llvm::BasicBlock& block : llvm::reverse(function))
				{
					SlotSet live(slots.size());
					for (const llvm::BasicBlock* next : llvm::successors(&block)) live.Join(in.at(next));
					out.at(&block) = live;
					for (const llvm::Instruction& instruction : llvm::reverse(block))
					{
						TouchesOf(instruction, slots, of, touches);
						for (const Touch& touch : touches) touch.reads ? live.Set(touch.slot) : live.Reset(touch.slot);
					}
					if (!(live == in.at(&block)))
					{
						in.at(&block) = live;
						changed = true;
					}
				}
			}
			return out;
		}
	}

	std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>> FrameSlotsAt(llvm::Function& function, const std::vector<llvm::CallBase*>& calls)
	{
		const llvm::DataLayout& layout = function.getDataLayout();
		std::vector<Slot> slots;
		std::unordered_map<const llvm::Value*, size_t> of;
		for (llvm::Instruction& instruction : llvm::instructions(function))
		{
			auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(&instruction);
			if (!alloca || !MayHoldAny(alloca->getAllocatedType())) continue;
			const std::optional<llvm::TypeSize> size = alloca->getAllocationSize(layout);
			if (!size || size->isScalable()) continue; // C4 makes every slot in the entry block, of a size it knows
			Slot slot{ alloca, size->getFixedValue(), Operands(*alloca, size->getFixedValue()), false };
			if (slot.operands.empty()) continue;
			slot.escapes = Escapes(alloca, slots.size(), of);
			slots.push_back(std::move(slot));
		}
		std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>> listed;
		if (slots.empty()) return listed;
		const std::unordered_map<const llvm::BasicBlock*, SlotSet> liveOut = LiveOut(function, slots, of);
		std::unordered_map<const llvm::Instruction*, SlotSet> liveAfter;
		std::vector<Touch> touches;
		for (llvm::BasicBlock& block : function)
		{
			SlotSet live = liveOut.at(&block);
			for (const llvm::Instruction& instruction : llvm::reverse(block))
			{
				if (llvm::isa<llvm::CallBase>(instruction)) liveAfter.emplace(&instruction, live);
				TouchesOf(instruction, slots, of, touches);
				for (const Touch& touch : touches) touch.reads ? live.Set(touch.slot) : live.Reset(touch.slot);
			}
		}
		for (llvm::CallBase* call : calls)
		{
			const SlotSet& live = liveAfter.at(call);
			std::vector<llvm::Value*> operands;
			for (size_t i = 0; i < slots.size(); ++i)
				if (slots[i].escapes || live.Has(i)) operands.insert(operands.end(), slots[i].operands.begin(), slots[i].operands.end());
			if (!operands.empty()) listed.emplace(call, std::move(operands));
		}
		return listed;
	}
}
