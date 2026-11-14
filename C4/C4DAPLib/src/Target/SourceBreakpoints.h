#pragma once
#include "Target/Patches.h"
#include "Target/SessionTypes.h"
#include "Target/Symbols.h"
#include <map>
#include <string>
#include <vector>

namespace C4DAP::Target
{
	/// The breakpoints the client sets per source file, bound to code addresses once the program's symbols are
	/// loaded.
	class SourceBreakpoints
	{
	public:
		/// Replaces `path`'s breakpoints with `lines`, binding them when `symbols` are loaded.
		std::vector<BoundBreakpoint> Set(const std::string& path, const std::vector<uint32_t>& lines, const Symbols* symbols, const Process& process,
			Patches& patches);
		/// Binds the breakpoints set before the program's code was loaded; returns every one it tried.
		std::vector<BoundBreakpoint> BindPending(const Symbols& symbols, const Process& process, Patches& patches);
		/// A module has loaded (a script's DLL, its next build): each breakpoint of a source it holds binds in it too, on the
		/// line it has for what was asked. Returns the breakpoints whose binding the client has to hear of.
		std::vector<BoundBreakpoint> BindInModule(uint64_t module, const Symbols& symbols, const Process& process, Patches& patches);
		/// `size` bytes from `base` have unloaded: what was bound only there waits for the code to come back. Returns those.
		std::vector<BoundBreakpoint> ForgetRange(uint64_t base, uint64_t size);
		/// The client's ids of the breakpoints at `address`.
		std::vector<uint32_t> IdsAt(uint64_t address) const;
		/// Whether a breakpoint at `address` is on `position`'s line.
		bool SetOn(uint64_t address, const SourcePosition& position) const;

	private:
		struct Entry
		{
			BoundBreakpoint reported;
			uint32_t requested = 0;
			std::vector<uint64_t> addresses;
		};
		void Bind(Entry& entry, const Symbols& symbols, const Process& process, Patches& patches);

		std::map<std::string, std::vector<Entry>> m_BySource; // keyed by the path in one spelling
		uint32_t m_NextId = 1;
	};
} // namespace C4DAP::Target
