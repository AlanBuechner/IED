#pragma once
#include "Target/Process.h"
#include <cstdint>
#include <unordered_map>

namespace C4DAP::Target
{
	/// The `int3` patches in the debuggee's code. Several users (source breakpoints, a step's return breakpoint,
	/// the entry stop) can share an address; the patch stays until the last one releases it.
	class Patches
	{
	public:
		bool Add(const Process& process, uint64_t address);
		void Release(const Process& process, uint64_t address);
		bool Has(uint64_t address) const { return m_Patches.count(address) != 0; }

		/// Puts the original byte back so the thread can execute the instruction; Restore re-patches it.
		void Lift(const Process& process, uint64_t address);
		void Restore(const Process& process, uint64_t address);

	private:
		struct Patch
		{
			uint8_t original = 0;
			uint32_t users = 0;
			bool lifted = false;
		};
		std::unordered_map<uint64_t, Patch> m_Patches;
	};
} // namespace C4DAP::Target
