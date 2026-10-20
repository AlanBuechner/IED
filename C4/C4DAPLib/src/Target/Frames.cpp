#include "Target/Frames.h"
#include <dbghelp.h>
#include <cstring>

namespace C4DAP::Target
{
	namespace
	{
		// CodeView register numbers for x64 (cvconst.h CV_AMD64_*).
		constexpr uint32_t kRip = 33;
		constexpr uint32_t kEaxFirst = 17, kEdiLast = 24;  // eax, ecx, edx, ebx, esp, ebp, esi, edi
		constexpr uint32_t kXmm0 = 154, kXmm15 = 169;
		constexpr uint32_t kRax = 328, kRsp = 335, kR8 = 336, kR15 = 343;
		constexpr uint32_t kR8d = 360, kR15d = 367;
		constexpr uint64_t kLow32 = 0xFFFFFFFFull;

		// The general registers in CONTEXT order: rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8..r15.
		DWORD64 General(const CONTEXT& c, uint32_t index)
		{
			const DWORD64* first = &c.Rax;
			return first[index];
		}

		// CodeView numbers rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp; CONTEXT has rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi.
		constexpr uint32_t kCodeViewToContext[] = { 0, 3, 1, 2, 6, 7, 5, 4 };
	}

	std::optional<uint64_t> RegisterValue(const CONTEXT& c, uint32_t reg)
	{
		if (reg == kRip) return c.Rip;
		if (reg >= kRax && reg <= kRsp) return General(c, kCodeViewToContext[reg - kRax]);
		if (reg >= kR8 && reg <= kR15) return General(c, 8 + (reg - kR8));
		if (reg >= kEaxFirst && reg <= kEdiLast) return General(c, reg - kEaxFirst) & kLow32;
		if (reg >= kR8d && reg <= kR15d) return General(c, 8 + (reg - kR8d)) & kLow32;
		if (reg >= kXmm0 && reg <= kXmm15)
		{
			uint64_t low = 0;
			std::memcpy(&low, &(&c.Xmm0)[reg - kXmm0], sizeof(low));
			return low;
		}
		return std::nullopt;
	}

	std::optional<ValueRef> VariableValue(const VariableSymbol& variable, const FrameRegisters& frame)
	{
		ValueRef value;
		value.type = variable.type;
		switch (variable.storage.kind)
		{
		case SymbolStorage::Kind::Address:
			value.address = static_cast<uint64_t>(variable.storage.offset);
			return value;
		case SymbolStorage::Kind::FrameRelative:
			value.address = frame.frameBase + variable.storage.offset;
			return value;
		case SymbolStorage::Kind::RegisterRelative:
		{
			const std::optional<uint64_t> base = RegisterValue(frame.registers, variable.storage.reg);
			if (!base) return std::nullopt;
			value.address = *base + variable.storage.offset;
			return value;
		}
		case SymbolStorage::Kind::Register:
		{
			const std::optional<uint64_t> bits = RegisterValue(frame.registers, variable.storage.reg);
			if (!bits) return std::nullopt;
			value.inRegister = true;
			value.registerBits = *bits;
			return value;
		}
		}
		return std::nullopt;
	}

	std::vector<FrameRegisters> WalkStack(const Process& process, DWORD thread, size_t limit)
	{
		std::vector<FrameRegisters> frames;
		CONTEXT walk{};
		if (!process.Registers(thread, walk)) return frames;
		STACKFRAME_EX frame{};
		frame.StackFrameSize = sizeof(frame);
		frame.AddrPC.Offset = walk.Rip;
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrStack.Offset = walk.Rsp;
		frame.AddrStack.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = walk.Rbp;
		frame.AddrFrame.Mode = AddrModeFlat;
		while (frames.size() < limit)
		{
			// The registers a frame had are the ones whose rip is its pc: the walk leaves the context either at
			// the frame it returns or already at the caller, so both are checked.
			const CONTEXT before = walk;
			if (!StackWalkEx(IMAGE_FILE_MACHINE_AMD64, process.Handle(), process.Thread(thread), &frame, &walk, nullptr, SymFunctionTableAccess64,
				SymGetModuleBase64, nullptr, SYM_STKWALK_DEFAULT) || frame.AddrPC.Offset == 0)
				break;
			FrameRegisters record;
			record.pc = frame.AddrPC.Offset;
			record.frameBase = frame.AddrFrame.Offset;
			record.registers = before.Rip == frame.AddrPC.Offset ? before : walk;
			frames.push_back(record);
		}
		return frames;
	}
} // namespace C4DAP::Target
