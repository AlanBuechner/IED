#pragma once
#include "Target/Process.h"
#include "Target/Symbols.h"
#include "Target/Values.h"
#include <optional>
#include <vector>

namespace C4DAP::Target
{
	/// One frame of a thread's stack and the registers it had.
	struct FrameRegisters
	{
		uint64_t pc = 0;
		uint64_t frameBase = 0;
		CONTEXT registers{};
		std::vector<uint32_t> inlined; // the inline frame contexts of the calls inlined at pc, innermost first
		/// Where to look up the frame's line and scope: a caller's pc is its return address, which may already
		/// belong to the next line, so the call instruction's last byte stands for it.
		uint64_t Lookup(size_t depth) const { return depth == 0 ? pc : pc - 1; }
	};

	/// The frames of `thread` that are calls (each with the calls inlined at its pc), innermost first, at most `limit`
	/// (the symbols of its modules must be loaded).
	std::vector<FrameRegisters> WalkStack(const Process& process, DWORD thread, size_t limit);

	/// A CodeView register's value in `registers`.
	std::optional<uint64_t> RegisterValue(const CONTEXT& registers, uint32_t codeViewRegister);

	/// Where a variable of `frame` is, from its symbol; none when it is optimized away there.
	std::optional<ValueRef> VariableValue(const VariableSymbol& variable, const FrameRegisters& frame);
} // namespace C4DAP::Target
