#include "Target/Frames.h"
#include <dbghelp.h>
#include <cstring>
#include <utility>

namespace C4DAP::Target
{
	namespace
	{
		// CodeView register numbers for x64 (cvconst.h CV_AMD64_*).
		constexpr uint32_t kRip = 33;
		constexpr uint32_t kRax = 328, kRsp = 335;
		constexpr uint64_t kAll = ~0ull, kLow32 = 0xFFFFFFFFull, kLow16 = 0xFFFFull, kLow8 = 0xFFull;
		constexpr uint32_t kHighByte = 8;

		// CodeView numbers that name consecutive general registers, or the same part of each: the CONTEXT index of
		// the first, and where the part's bits are.
		struct GeneralRun
		{
			uint32_t first;
			uint32_t last;
			uint32_t contextIndex;
			uint32_t shift;
			uint64_t mask;
		};

		// The legacy registers' CodeView order (a, c, d, b, sp, bp, si, di) is CONTEXT's.
		constexpr GeneralRun kGeneralRuns[] = {
			{ 1, 4, 0, 0, kLow8 },           // al, cl, dl, bl
			{ 5, 8, 0, kHighByte, kLow8 },   // ah, ch, dh, bh
			{ 9, 16, 0, 0, kLow16 },         // ax .. di
			{ 17, 24, 0, 0, kLow32 },        // eax .. edi
			{ 324, 325, 6, 0, kLow8 },       // sil, dil
			{ 326, 326, 5, 0, kLow8 },       // bpl
			{ 327, 327, 4, 0, kLow8 },       // spl
			{ 336, 343, 8, 0, kAll },        // r8 .. r15
			{ 344, 351, 8, 0, kLow8 },       // r8b .. r15b
			{ 352, 359, 8, 0, kLow16 },      // r8w .. r15w
			{ 360, 367, 8, 0, kLow32 },      // r8d .. r15d
		};

		// xmm0-xmm7 and xmm8-xmm15 are apart in CodeView's numbers (the lanes of xmm0 and xmm1 come between).
		struct VectorRun
		{
			uint32_t first;
			uint32_t last;
			uint32_t xmmIndex;
		};

		constexpr VectorRun kVectorRuns[] = { { 154, 161, 0 }, { 252, 259, 8 } };

		// The general registers in CONTEXT order: rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8..r15.
		DWORD64 General(const CONTEXT& c, uint32_t index)
		{
			const DWORD64* first = &c.Rax;
			return first[index];
		}

		uint64_t XmmLow(const CONTEXT& c, uint32_t index)
		{
			uint64_t low = 0;
			std::memcpy(&low, &(&c.Xmm0)[index], sizeof(low));
			return low;
		}

		// CodeView numbers rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp; CONTEXT has rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi.
		constexpr uint32_t kCodeViewToContext[] = { 0, 3, 1, 2, 6, 7, 5, 4 };

		// StackWalkEx's InlineFrameContext: its second byte is the frame's type, where DbgEng's STACK_FRAME_TYPE_INLINE
		// marks a call the optimizer inlined (the walk reports it as a frame of its own, at the same pc).
		constexpr uint32_t kFrameTypeShift = 8;
		constexpr uint32_t kInlineFrameType = 0x02;

		bool IsInlineFrame(uint32_t inlineFrameContext) { return ((inlineFrameContext >> kFrameTypeShift) & kInlineFrameType) != 0; }
	}

	std::optional<uint64_t> RegisterValue(const CONTEXT& c, uint32_t reg)
	{
		if (reg == kRip) return c.Rip;
		if (reg >= kRax && reg <= kRsp) return General(c, kCodeViewToContext[reg - kRax]);
		for (const GeneralRun& run : kGeneralRuns)
			if (reg >= run.first && reg <= run.last) return (General(c, run.contextIndex + (reg - run.first)) >> run.shift) & run.mask;
		for (const VectorRun& run : kVectorRuns)
			if (reg >= run.first && reg <= run.last) return XmmLow(c, run.xmmIndex + (reg - run.first));
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
		case SymbolStorage::Kind::Constant:
			value.inRegister = true;
			value.registerBits = variable.storage.constant;
			return value;
		case SymbolStorage::Kind::OptimizedAway: return std::nullopt;
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
		std::vector<uint32_t> inlined;
		while (frames.size() < limit)
		{
			// The registers a frame had are the ones whose rip is its pc: the walk leaves the context either at
			// the frame it returns or already at the caller, so both are checked.
			const CONTEXT before = walk;
			if (!StackWalkEx(IMAGE_FILE_MACHINE_AMD64, process.Handle(), process.Thread(thread), &frame, &walk, nullptr, SymFunctionTableAccess64,
				SymGetModuleBase64, nullptr, SYM_STKWALK_DEFAULT) || frame.AddrPC.Offset == 0)
				break;
			if (IsInlineFrame(frame.InlineFrameContext))
			{
				inlined.push_back(frame.InlineFrameContext); // the walk gives them before the frame they are in
				continue;
			}
			FrameRegisters record;
			record.pc = frame.AddrPC.Offset;
			record.frameBase = frame.AddrFrame.Offset;
			record.registers = before.Rip == frame.AddrPC.Offset ? before : walk;
			record.inlined = std::exchange(inlined, {});
			frames.push_back(record);
		}
		return frames;
	}
} // namespace C4DAP::Target
