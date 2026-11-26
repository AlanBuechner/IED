#include "Compiler/ProcessMemory.h"
#include <algorithm>
#include <malloc.h> // _heapmin on Windows, malloc_trim on Linux (glibc)

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <cstdlib>
#include <fstream>
#include <string>
#endif

namespace C4Compiler
{
#ifdef _WIN32
	std::optional<SystemMemory> QuerySystemMemory()
	{
		MEMORYSTATUSEX status{};
		status.dwLength = sizeof(status);
		if (!GlobalMemoryStatusEx(&status)) return std::nullopt;
		return SystemMemory{ status.ullAvailPhys, status.ullAvailPageFile };
	}

	uint64_t HeldMemory()
	{
		PROCESS_MEMORY_COUNTERS_EX counters{};
		counters.cb = sizeof(counters);
		if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return 0;
		return counters.PrivateUsage;
	}

	// The CRT heap's free blocks only: the compiler's own allocations go through LLVM's rpmalloc (the vendored LLVM is built
	// with LLVM_INTEGRATED_CRT_ALLOC, whose malloc replaces the static CRT's), which keeps what is freed for the process's
	// next allocations and has no call that hands it back (docs/compiler/07 "The server's memory").
	void ReturnFreedMemory()
	{
		_heapmin();
	}
#else
	namespace
	{
		constexpr uint64_t c_BytesPerKilobyte = 1024;

		// The number of a "<key>: <n> kB" line of a /proc file, in bytes; nothing when the file has no such line.
		std::optional<uint64_t> ProcKilobytes(const char* path, const std::string& key)
		{
			std::ifstream file(path);
			const std::string prefix = key + ":";
			for (std::string line; std::getline(file, line);)
				if (line.compare(0, prefix.size(), prefix) == 0)
					return std::strtoull(line.c_str() + prefix.size(), nullptr, 10) * c_BytesPerKilobyte;
			return std::nullopt;
		}
	}

	// Linux commits lazily: what is left is what the kernel could still hand out, memory it can reclaim and free swap.
	std::optional<SystemMemory> QuerySystemMemory()
	{
		const std::optional<uint64_t> available = ProcKilobytes("/proc/meminfo", "MemAvailable");
		if (!available) return std::nullopt;
		return SystemMemory{ *available, *available + ProcKilobytes("/proc/meminfo", "SwapFree").value_or(0) };
	}

	uint64_t HeldMemory()
	{
		return ProcKilobytes("/proc/self/status", "VmRSS").value_or(0) + ProcKilobytes("/proc/self/status", "VmSwap").value_or(0);
	}

	void ReturnFreedMemory()
	{
		malloc_trim(0);
	}
#endif

	bool MachineNeedsMemory(const SystemMemory& memory, uint64_t heldBytes)
	{
		return std::min(memory.availablePhysical, memory.availableCommit) < heldBytes;
	}
} // namespace C4Compiler
