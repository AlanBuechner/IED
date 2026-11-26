#pragma once
#include "Compiler/ProcessMemory.h"
#include <cstdint>
#include <optional>

namespace C4Compiler
{
	/// How long a build server waits without a build before it hands its memory back to a machine short of it
	/// (MachineNeedsMemory): the program built last keeps its server, the others' go first.
	constexpr unsigned c_ShortMemoryIdleSeconds = 120;

	/// How many times what it held after its first build a server may hold before it ends, so that its program's next
	/// build starts a new one: the allocator keeps what a process frees (ReturnFreedMemory), and each cold build in a
	/// server leaves some of it behind.
	constexpr uint64_t c_OutgrownFactor = 2;

	/// Why a build server (C4.exe --serve-on, docs/compiler/07) waiting for its next build stops waiting.
	enum class ServerWait
	{
		Waiting,
		Idle,         // --serve-idle seconds without a build
		FolderGone,   // its program's folder was removed: nothing is built there again
		Outgrown,     // it holds more than c_OutgrownFactor times what it held after its first build
		MemoryWanted, // the machine has less memory left than the server holds, and no build came for a while
	};

	/// What a waiting server sees each time it looks.
	struct WaitingServer
	{
		unsigned waitedSeconds = 0;          // since its last build, or since it started
		unsigned idleSeconds = 0;            // --serve-idle
		bool folderStays = true;
		std::optional<SystemMemory> machine; // nothing when the system does not say
		uint64_t heldBytes = 0;              // what the server holds: its last build
		uint64_t firstBuildBytes = 0;        // what it held after its first build that succeeded; 0 before one
	};

	ServerWait JudgeWait(const WaitingServer& server);
} // namespace C4Compiler
