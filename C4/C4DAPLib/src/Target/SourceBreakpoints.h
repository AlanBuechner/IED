#pragma once
#include "Target/Patches.h"
#include "Target/Symbols.h"
#include <map>
#include <string>
#include <vector>

namespace C4DAP::Target
{
	/// A source breakpoint as the client sees it: bound (verified) to the line it stops on, or not with why.
	struct BoundBreakpoint
	{
		uint32_t id = 0;
		bool verified = false;
		uint32_t line = 0;
		std::string source;
		std::string message;
	};

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
		/// The client's ids of the breakpoints at `address`.
		std::vector<uint32_t> IdsAt(uint64_t address) const;

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
