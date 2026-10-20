#include "Target/CatchHandlers.h"
#include <dbghelp.h>

namespace C4DAP::Target
{
	namespace
	{
		// x64 UNWIND_INFO: version and flags, prolog size, unwind code count, frame register; the codes (2 bytes
		// each, padded to an even count), then the handler's RVA and its language-specific data.
		constexpr uint8_t kHandlerFlags = 0x3;  // UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER
		constexpr uint8_t kChainFlag = 0x4;     // UNW_FLAG_CHAININFO
		constexpr uint64_t kUnwindHeader = 4;
		constexpr uint64_t kUnwindCodeBytes = 2;
		constexpr int kMaxChain = 32;
		// FuncInfo (__CxxFrameHandler3): nTryBlocks at 12, dispTryBlockMap at 16. A TryBlockMapEntry is 20 bytes,
		// nCatches at 12 and dispHandlerArray at 16; a HandlerType is 20 bytes, dispOfHandler at 12.
		constexpr uint64_t kTryBlockCount = 12, kTryBlockMap = 16, kTryBlockBytes = 20;
		constexpr uint64_t kCatchCount = 12, kHandlerArray = 16, kHandlerBytes = 20, kHandlerAddress = 12;
		constexpr uint32_t kMaxEntries = 4096; // a table claiming more is not one

		struct Image
		{
			const Process& process;
			uint64_t base;
			template <typename T> bool Read(uint64_t rva, T& value) const { return process.Read(base + rva, value); }
		};

		// The unwind information of the function at `pc`, following chained entries to the primary one.
		bool UnwindInfo(const Image& image, uint64_t pc, uint32_t& unwindRva)
		{
			auto* function = static_cast<const RUNTIME_FUNCTION*>(SymFunctionTableAccess64(image.process.Handle(), pc));
			if (!function) return false;
			unwindRva = function->UnwindData;
			for (int depth = 0; depth < kMaxChain; ++depth)
			{
				uint8_t flags = 0, codes = 0;
				if (!image.Read(unwindRva, flags) || !image.Read(unwindRva + 2, codes)) return false;
				if (!((flags >> 3) & kChainFlag)) return true;
				const uint64_t chained = unwindRva + kUnwindHeader + ((codes + 1u) & ~1u) * kUnwindCodeBytes;
				RUNTIME_FUNCTION primary{};
				if (!image.Read(chained, primary)) return false;
				unwindRva = primary.UnwindData;
			}
			return false;
		}

		// The FuncInfo of a function whose handler takes one (the C++ handler's language-specific data).
		bool FuncInfo(const Image& image, uint32_t unwindRva, uint32_t& funcInfoRva)
		{
			uint8_t flags = 0, codes = 0;
			if (!image.Read(unwindRva, flags) || !image.Read(unwindRva + 2, codes) || !((flags >> 3) & kHandlerFlags)) return false;
			const uint64_t handlerData = unwindRva + kUnwindHeader + ((codes + 1u) & ~1u) * kUnwindCodeBytes;
			return image.Read(handlerData + sizeof(uint32_t), funcInfoRva) && funcInfoRva != 0;
		}
	}

	std::vector<uint64_t> CatchHandlersOf(const Process& process, uint64_t pc)
	{
		std::vector<uint64_t> handlers;
		const Image image{ process, SymGetModuleBase64(process.Handle(), pc) };
		uint32_t unwindRva = 0, funcInfo = 0, tryBlocks = 0;
		int32_t tryMap = 0;
		if (!image.base || !UnwindInfo(image, pc, unwindRva) || !FuncInfo(image, unwindRva, funcInfo)) return handlers;
		if (!image.Read(funcInfo + kTryBlockCount, tryBlocks) || !image.Read(funcInfo + kTryBlockMap, tryMap) || tryBlocks > kMaxEntries) return handlers;
		for (uint32_t block = 0; block < tryBlocks; ++block)
		{
			const uint64_t entry = static_cast<uint32_t>(tryMap) + block * kTryBlockBytes;
			uint32_t catches = 0;
			int32_t handlerArray = 0;
			if (!image.Read(entry + kCatchCount, catches) || !image.Read(entry + kHandlerArray, handlerArray) || catches > kMaxEntries) continue;
			for (uint32_t i = 0; i < catches; ++i)
			{
				int32_t address = 0;
				if (image.Read(static_cast<uint32_t>(handlerArray) + i * kHandlerBytes + kHandlerAddress, address) && address != 0)
					handlers.push_back(image.base + static_cast<uint32_t>(address));
			}
		}
		return handlers;
	}
} // namespace C4DAP::Target
