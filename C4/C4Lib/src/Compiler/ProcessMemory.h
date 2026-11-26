#pragma once
#include <cstdint>
#include <optional>

namespace C4Compiler
{
	/// The memory the machine has left now, in bytes, as the system counts it.
	struct SystemMemory
	{
		uint64_t availablePhysical = 0;
		uint64_t availableCommit = 0; // what processes may still commit: what is left of physical memory and the page file
	};

	/// The machine's memory now; nothing when the system does not say.
	std::optional<SystemMemory> QuerySystemMemory();

	/// What this process holds: its private committed bytes (on Linux, its resident and swapped bytes).
	uint64_t HeldMemory();

	/// Whether memory a long-lived compiler keeps for its next request (`heldBytes`: a build server's last build) is
	/// wanted back: the machine has less left than it holds, so the next program that needs that much finds none.
	bool MachineNeedsMemory(const SystemMemory& memory, uint64_t heldBytes);

	/// Hands what the heap keeps of freed allocations back to the system: the heap otherwise keeps it for the next
	/// allocation, and a let-go build or analysis is gigabytes the process may not need again. On Linux (glibc) it does;
	/// on Windows the compiler's allocator (LLVM's rpmalloc) keeps freed memory for this process's next allocations
	/// whatever is called, so a long-lived process there holds about the most it ever used.
	void ReturnFreedMemory();
} // namespace C4Compiler
