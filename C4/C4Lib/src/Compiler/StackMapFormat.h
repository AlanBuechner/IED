#pragma once
#include <cstdint>

namespace llvm { class AsmPrinter; class StackMaps; }

namespace C4Compiler
{
	/// The program's stack maps as the runtime reads them (GC/GCStackMaps.c), written by code generation in place of
	/// LLVM's own format, whose 12 bytes a location made the IED server's executable 5 times as large. One map an
	/// object; the linker puts them one after another, each 8-byte aligned, zeros between:
	/// - a header: u32 c_StackMapMagic, u32 c_StackMapVersion, u32 its functions, u32 its bytes (header included);
	/// - each function's address, a u64 the linker relocates;
	/// - then function by function: ULEB its records, ULEB its slots; each slot - a stack slot its records may list -
	///   as ULEB (register << 2 | base: StackMapSlotBase), SLEB offset, ULEB (words << 1 | read every word), then
	///   unless every word is read a ULEB mask per 32 words of those that may hold a pointer; then each record - u32 the
	///   return address's offset in the function, ULEB (values << 1 | read the frame whole), each value as ULEB
	///   (register << 1 | in memory) with a SLEB offset when in memory, then a bit per slot of the function, set for those
	///   the record lists ((slots + 7) / 8 bytes). A record "read the frame whole" says no more.
	/// Registers are DWARF's numbers. A value in memory is the word at register + offset.
	constexpr uint32_t c_StackMapMagic = 0x4D533443; // "C4SM"
	constexpr uint32_t c_StackMapVersion = 1;
	enum class StackMapSlotBase : uint8_t
	{
		Direct,   // the slot starts at register + offset
		Indirect, // the word at register + offset holds the slot's address
		Register, // the register holds the slot's address
	};

	/// The section of the maps: an image keeps a COFF object's `.c4smap` as such; an ELF object's `c4_stackmaps` is
	/// writable (a position-independent program relocates the functions' addresses as it loads), kept by the linker's
	/// garbage collection, and bracketed by the linker's __start_/__stop_ symbols, which the runtime finds it by.
	constexpr const char* c_StackMapSectionCoff = ".c4smap";
	constexpr const char* c_StackMapSectionElf = "c4_stackmaps";

	/// Writes `maps` - the records of the statepoints `printer` lowered (Compiler/StackMaps: each a deopt list) - into
	/// the object, and clears them.
	void EmitStackMaps(llvm::StackMaps& maps, llvm::AsmPrinter& printer);
}
