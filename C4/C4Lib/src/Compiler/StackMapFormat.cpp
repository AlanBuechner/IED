#include "Compiler/StackMapFormat.h"
#include "Compiler/StackMapSlots.h"
#include <llvm/BinaryFormat/COFF.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/CodeGen/AsmPrinter.h>
#include <llvm/CodeGen/StackMaps.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCExpr.h>
#include <llvm/MC/MCSectionCOFF.h>
#include <llvm/MC/MCSectionELF.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCSymbol.h>
#include <llvm/TargetParser/Triple.h>
#include <bit>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace C4Compiler
{
	namespace
	{
		using Location = llvm::StackMaps::Location;
		using Locations = llvm::StackMaps::LocationVec;

		// A statepoint's first locations, LLVM's: its calling convention, its flags, its count of deopt operands.
		constexpr size_t c_StatepointHeader = 3;
		constexpr size_t c_DeoptCount = 2;
		constexpr uint32_t c_MaskWords = 32;
		constexpr uint64_t c_MapAlign = 8;
		constexpr unsigned c_FieldBytes = 4; // the header's fields and a record's return offset

		struct Slot
		{
			StackMapSlotBase base = StackMapSlotBase::Direct;
			uint16_t reg = 0;
			int32_t offset = 0;
			uint32_t words = 0;
			bool whole = false;
			std::vector<uint32_t> masks;
		};

		struct Value
		{
			uint16_t reg = 0;
			bool inMemory = false;
			int32_t offset = 0;
		};

		// A record's deopt list (Compiler/StackMapRecords) as read from its locations.
		struct DeoptList
		{
			std::vector<Value> values;
			std::vector<Slot> slots;
		};

		struct Record
		{
			const llvm::MCExpr* offset = nullptr;
			bool whole = false; // its locations say what this format cannot: the runtime reads the frame whole
			std::vector<Value> values;
			std::vector<size_t> slots; // indices into its function's slots
		};

		bool IsConstant(const Location& location) { return location.Type == Location::Constant; }
		uint32_t ConstantOf(const Location& location) { return static_cast<uint32_t>(location.Offset); }

		// A live value's place: a register, or a word in memory - a spill slot (Indirect), or an incoming argument's own
		// stack slot (Direct: LLVM names a stack-passed argument by its slot). A constant names no object and adds
		// nothing. False for any other place.
		bool ReadValue(const Location& location, std::vector<Value>& values)
		{
			switch (location.Type)
			{
			case Location::Register: values.push_back({ location.Reg, false, 0 }); return true;
			case Location::Direct:
			case Location::Indirect: values.push_back({ location.Reg, true, location.Offset }); return true;
			case Location::Constant: return true;
			default: return false;
			}
		}

		std::optional<StackMapSlotBase> SlotBaseOf(const Location& location)
		{
			switch (location.Type)
			{
			case Location::Direct: return StackMapSlotBase::Direct;
			case Location::Indirect: return StackMapSlotBase::Indirect;
			case Location::Register: return StackMapSlotBase::Register;
			default: return std::nullopt;
			}
		}

		// The slot that starts at `at[i]`: its place, its word count, its masks; `i` moves past them.
		std::optional<Slot> ReadSlot(const Locations& at, size_t& i, size_t end)
		{
			if (i + 1 >= end || !IsConstant(at[i + 1])) return std::nullopt;
			const std::optional<StackMapSlotBase> base = SlotBaseOf(at[i]);
			if (!base) return std::nullopt;
			const uint32_t words = ConstantOf(at[i + 1]);
			Slot slot{ *base, at[i].Reg, *base == StackMapSlotBase::Register ? 0 : at[i].Offset, words & ~c_WholeSlot, (words & c_WholeSlot) != 0, {} };
			i += 2;
			if (slot.whole) return slot;
			for (uint32_t first = 0; first < slot.words; first += c_MaskWords, ++i)
			{
				if (i >= end || !IsConstant(at[i])) return std::nullopt;
				slot.masks.push_back(ConstantOf(at[i]));
			}
			return slot;
		}

		// nullopt when the locations are no such list (a statepoint of another kind).
		std::optional<DeoptList> ReadDeoptList(const Locations& at)
		{
			if (at.size() <= c_StatepointHeader || !IsConstant(at[c_DeoptCount]) || !IsConstant(at[c_StatepointHeader])) return std::nullopt;
			const size_t end = c_StatepointHeader + ConstantOf(at[c_DeoptCount]);
			const size_t valuesEnd = c_StatepointHeader + 1 + ConstantOf(at[c_StatepointHeader]);
			if (end > at.size() || valuesEnd > end) return std::nullopt;
			DeoptList list;
			for (size_t i = c_StatepointHeader + 1; i < valuesEnd; ++i)
				if (!ReadValue(at[i], list.values)) return std::nullopt;
			for (size_t i = valuesEnd; i < end;)
			{
				std::optional<Slot> slot = ReadSlot(at, i, end);
				if (!slot) return std::nullopt;
				list.slots.push_back(std::move(*slot));
			}
			return list;
		}

		std::string KeyOf(const Slot& slot)
		{
			std::string key;
			auto put = [&key](const auto& field) { key.append(reinterpret_cast<const char*>(&field), sizeof field); };
			put(slot.base);
			put(slot.reg);
			put(slot.offset);
			put(slot.words);
			put(slot.whole);
			for (uint32_t mask : slot.masks) put(mask);
			return key;
		}

		void EmitSlot(llvm::MCStreamer& out, const Slot& slot)
		{
			out.emitULEB128IntValue(static_cast<uint64_t>(slot.reg) << 2 | static_cast<uint64_t>(slot.base));
			out.emitSLEB128IntValue(slot.offset);
			out.emitULEB128IntValue(static_cast<uint64_t>(slot.words) << 1 | (slot.whole ? 1u : 0u));
			for (uint32_t mask : slot.masks) out.emitULEB128IntValue(mask);
		}

		void EmitRecord(llvm::MCStreamer& out, const Record& record, size_t slots)
		{
			out.emitValue(record.offset, c_FieldBytes);
			if (record.whole)
			{
				out.emitULEB128IntValue(1);
				return;
			}
			out.emitULEB128IntValue(static_cast<uint64_t>(record.values.size()) << 1);
			for (const Value& value : record.values)
			{
				out.emitULEB128IntValue(static_cast<uint64_t>(value.reg) << 1 | (value.inMemory ? 1u : 0u));
				if (value.inMemory) out.emitSLEB128IntValue(value.offset);
			}
			std::string listed((slots + 7) / 8, '\0');
			for (size_t slot : record.slots) listed[slot / 8] = static_cast<char>(listed[slot / 8] | (1 << (slot % 8)));
			out.emitBytes(listed);
		}

		bool IsSlotTable(const llvm::StackMaps::CallsiteInfo& call) { return call.ID == c_SlotTableId; }

		// One function's map: its slots, each once (its slot table's first, then in the order its records first list them),
		// then its records.
		class FunctionMap
		{
		public:
			// The function's slot table (c_SlotTableId), which its call records' masks index.
			void AddTable(const llvm::StackMaps::CallsiteInfo& table)
			{
				const Locations& at = table.Locations;
				for (size_t i = 0; i < at.size();)
				{
					std::optional<Slot> slot = ReadSlot(at, i, at.size());
					if (!slot)
					{
						m_TableRead = false;
						return;
					}
					m_Table.push_back(IndexOf(std::move(*slot)));
				}
			}

			void Add(const llvm::StackMaps::CallsiteInfo& call)
			{
				Record record{ call.CSOffsetExpr };
				if (call.ID == c_CallRecordId) record.whole = !ReadMasks(call.Locations, record.slots);
				else if (std::optional<DeoptList> list = ReadDeoptList(call.Locations))
				{
					record.values = std::move(list->values);
					for (Slot& slot : list->slots) record.slots.push_back(IndexOf(std::move(slot)));
				}
				else record.whole = true;
				m_Records.push_back(std::move(record));
			}

			void Emit(llvm::MCStreamer& out) const
			{
				out.emitULEB128IntValue(m_Records.size());
				out.emitULEB128IntValue(m_Slots.size());
				for (const Slot& slot : m_Slots) EmitSlot(out, slot);
				for (const Record& record : m_Records) EmitRecord(out, record, m_Slots.size());
			}

		private:
			size_t IndexOf(Slot slot)
			{
				const auto [found, added] = m_Index.emplace(KeyOf(slot), m_Slots.size());
				if (added) m_Slots.push_back(std::move(slot));
				return found->second;
			}

			// A call record's slots: the table's slots its masks set. False when the masks name no slot of a table read.
			bool ReadMasks(const Locations& masks, std::vector<size_t>& slots) const
			{
				if (!m_TableRead || masks.size() * c_MaskWords < m_Table.size()) return false;
				for (size_t m = 0; m < masks.size(); ++m)
				{
					if (!IsConstant(masks[m])) return false;
					for (uint32_t bits = ConstantOf(masks[m]); bits; bits &= bits - 1u)
					{
						const size_t at = m * c_MaskWords + static_cast<size_t>(std::countr_zero(bits));
						if (at >= m_Table.size()) return false;
						slots.push_back(m_Table[at]);
					}
				}
				return true;
			}

			std::vector<Slot> m_Slots;
			std::unordered_map<std::string, size_t> m_Index;
			std::vector<size_t> m_Table; // the slot table's slots, by their index in m_Slots
			bool m_TableRead = true;
			std::vector<Record> m_Records;
		};

		llvm::MCSection* SectionFor(llvm::MCContext& context)
		{
			if (context.getTargetTriple().isOSBinFormatELF())
				return context.getELFSection(c_StackMapSectionElf, llvm::ELF::SHT_PROGBITS, llvm::ELF::SHF_ALLOC | llvm::ELF::SHF_WRITE | llvm::ELF::SHF_GNU_RETAIN);
			return context.getCOFFSection(c_StackMapSectionCoff, llvm::COFF::IMAGE_SCN_CNT_INITIALIZED_DATA | llvm::COFF::IMAGE_SCN_MEM_READ);
		}

		const llvm::MCExpr* Distance(llvm::MCSymbol* from, llvm::MCSymbol* to, llvm::MCContext& context)
		{
			return llvm::MCBinaryExpr::createSub(llvm::MCSymbolRefExpr::create(to, context), llvm::MCSymbolRefExpr::create(from, context), context);
		}
	}

	void EmitStackMaps(llvm::StackMaps& maps, llvm::AsmPrinter& printer)
	{
		const llvm::StackMaps::CallsiteInfoList& calls = maps.getCSInfos();
		if (calls.empty()) return;
		llvm::MCContext& context = printer.OutContext;
		llvm::MCStreamer& out = *printer.OutStreamer;
		out.switchSection(SectionFor(context));
		out.emitValueToAlignment(llvm::Align(c_MapAlign));
		llvm::MCSymbol* start = context.createTempSymbol();
		llvm::MCSymbol* end = context.createTempSymbol();
		out.emitLabel(start);
		const llvm::StackMaps::FnInfoMap& functions = maps.getFnInfos();
		out.emitIntValue(c_StackMapMagic, c_FieldBytes);
		out.emitIntValue(c_StackMapVersion, c_FieldBytes);
		out.emitIntValue(functions.size(), c_FieldBytes);
		out.emitValue(Distance(start, end, context), c_FieldBytes);
		for (const auto& [symbol, info] : functions) out.emitValue(llvm::MCSymbolRefExpr::create(symbol, context), sizeof(uint64_t));
		// The records are the functions' in turn, each function's in the order its calls were lowered; a slot table may come
		// after records that index it, so it is read first.
		size_t next = 0;
		for (const auto& [symbol, info] : functions)
		{
			FunctionMap map;
			for (uint64_t i = 0; i < info.RecordCount; ++i)
				if (IsSlotTable(calls[next + i])) map.AddTable(calls[next + i]);
			for (uint64_t i = 0; i < info.RecordCount; ++i)
				if (!IsSlotTable(calls[next + i])) map.Add(calls[next + i]);
			next += info.RecordCount;
			map.Emit(out);
		}
		out.emitValueToAlignment(llvm::Align(c_MapAlign));
		out.emitLabel(end);
		maps.reset();
	}
}
