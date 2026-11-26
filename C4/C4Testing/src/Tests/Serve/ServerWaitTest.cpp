#include "ServerWaitTest.h"
#include "Compiler/ProcessMemory.h"
#include "Serve/ServerWait.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		constexpr uint64_t c_Gigabyte = 1024ull * 1024 * 1024;
		constexpr unsigned c_IdleSeconds = 1800;

		// A server of `held` bytes that waited `waited` seconds, on a machine with `physical` and `commit` bytes left.
		WaitingServer Server(unsigned waited, uint64_t held, uint64_t physical, uint64_t commit)
		{
			WaitingServer server;
			server.waitedSeconds = waited;
			server.idleSeconds = c_IdleSeconds;
			server.machine = SystemMemory{ physical, commit };
			server.heldBytes = held;
			return server;
		}

		// Small blocks, as a build's trees are: they come from the allocator's own pages, which it keeps after a free.
		constexpr size_t c_BlockBytes = 256;
		constexpr size_t c_Blocks = 1024 * 1024; // 256 MB
		constexpr uint64_t c_Slack = 64ull * 1024 * 1024;

		// What this process holds while `c_Blocks` small blocks are alive; they are freed when it returns.
		uint64_t HeldWithBlocks()
		{
			std::vector<std::unique_ptr<char[]>> blocks;
			blocks.reserve(c_Blocks);
			for (size_t i = 0; i < c_Blocks; ++i)
			{
				blocks.push_back(std::make_unique<char[]>(c_BlockBytes));
				std::memset(blocks.back().get(), static_cast<int>(i), c_BlockBytes);
			}
			return HeldMemory();
		}
	}

	void ServerWaitTest::AServerWaitsUntilItsIdleTime()
	{
		const SystemMemory plenty{ 32 * c_Gigabyte, 64 * c_Gigabyte };
		WaitingServer server = Server(0, 4 * c_Gigabyte, plenty.availablePhysical, plenty.availableCommit);
		TEST_ASSERT(JudgeWait(server) == ServerWait::Waiting);
		server.waitedSeconds = c_IdleSeconds - 1;
		TEST_ASSERT(JudgeWait(server) == ServerWait::Waiting);
		server.waitedSeconds = c_IdleSeconds;
		TEST_ASSERT(JudgeWait(server) == ServerWait::Idle);
	}

	void ServerWaitTest::AServerWhoseFolderWentEndsAtOnce()
	{
		WaitingServer server = Server(0, 4 * c_Gigabyte, 32 * c_Gigabyte, 64 * c_Gigabyte);
		server.folderStays = false;
		TEST_ASSERT(JudgeWait(server) == ServerWait::FolderGone);
	}

	void ServerWaitTest::AServerKeepsItsMemoryWhileTheMachineHasMore()
	{
		const WaitingServer server = Server(c_IdleSeconds - 1, 15 * c_Gigabyte, 16 * c_Gigabyte, 40 * c_Gigabyte);
		TEST_ASSERT(JudgeWait(server) == ServerWait::Waiting);
	}

	void ServerWaitTest::AServerGivesItsMemoryBackAfterAWhileWhenTheMachineHasLess()
	{
		WaitingServer server = Server(0, 15 * c_Gigabyte, 14 * c_Gigabyte, 40 * c_Gigabyte);
		TEST_ASSERT(JudgeWait(server) == ServerWait::Waiting); // just built: its program is the one being worked on
		server.waitedSeconds = c_ShortMemoryIdleSeconds - 1;
		TEST_ASSERT(JudgeWait(server) == ServerWait::Waiting);
		server.waitedSeconds = c_ShortMemoryIdleSeconds;
		TEST_ASSERT(JudgeWait(server) == ServerWait::MemoryWanted);
	}

	void ServerWaitTest::TheTighterOfPhysicalMemoryAndCommitDecides()
	{
		TEST_ASSERT(MachineNeedsMemory(SystemMemory{ 40 * c_Gigabyte, 3 * c_Gigabyte }, 4 * c_Gigabyte));
		TEST_ASSERT(MachineNeedsMemory(SystemMemory{ 3 * c_Gigabyte, 40 * c_Gigabyte }, 4 * c_Gigabyte));
		TEST_ASSERT(!MachineNeedsMemory(SystemMemory{ 5 * c_Gigabyte, 6 * c_Gigabyte }, 4 * c_Gigabyte));
		TEST_ASSERT(!MachineNeedsMemory(SystemMemory{ 4 * c_Gigabyte, 4 * c_Gigabyte }, 4 * c_Gigabyte));
	}

	void ServerWaitTest::AMachineThatDoesNotSayKeepsTheServer()
	{
		WaitingServer server = Server(c_IdleSeconds - 1, 15 * c_Gigabyte, 0, 0);
		server.machine.reset();
		TEST_ASSERT(JudgeWait(server) == ServerWait::Waiting);
	}

	void ServerWaitTest::TheIdleTimeEndsAServerBeforeTheMemoryCheck()
	{
		WaitingServer server = Server(60, 15 * c_Gigabyte, 1 * c_Gigabyte, 1 * c_Gigabyte);
		server.idleSeconds = 60; // --serve-idle shorter than the wait before memory counts
		TEST_ASSERT(JudgeWait(server) == ServerWait::Idle);
	}

	void ServerWaitTest::AServerThatOutgrewItsFirstBuildEndsAtOnce()
	{
		WaitingServer server = Server(0, 8 * c_Gigabyte, 32 * c_Gigabyte, 64 * c_Gigabyte);
		server.firstBuildBytes = 4 * c_Gigabyte;
		TEST_ASSERT(JudgeWait(server) == ServerWait::Waiting); // twice its first build: still serving
		server.heldBytes += 1;
		TEST_ASSERT(JudgeWait(server) == ServerWait::Outgrown);
	}

	void ServerWaitTest::AServerWithoutABuildCannotOutgrowOne()
	{
		WaitingServer server = Server(0, 15 * c_Gigabyte, 32 * c_Gigabyte, 64 * c_Gigabyte);
		server.firstBuildBytes = 0;
		TEST_ASSERT(JudgeWait(server) == ServerWait::Waiting);
	}

	void ServerWaitTest::TheMachineAndThisProcessHaveMemory()
	{
		const std::optional<SystemMemory> machine = QuerySystemMemory();
		TEST_ASSERT(machine.has_value());
		TEST_ASSERT(machine->availablePhysical > 0);
		TEST_ASSERT(machine->availableCommit > 0);
		TEST_ASSERT(HeldMemory() > 0);
	}

	// What a build server's next build relies on: the memory its last one freed is taken again, not added to.
	void ServerWaitTest::FreedMemoryIsTakenAgainByTheNextAllocations()
	{
		ReturnFreedMemory();
		const uint64_t before = HeldMemory();
		const uint64_t first = HeldWithBlocks();
		TEST_ASSERT(first > before + c_Blocks * c_BlockBytes / 2);
		ReturnFreedMemory();
		TEST_ASSERT(HeldWithBlocks() < first + c_Slack);
	}

#ifndef _WIN32
	void ServerWaitTest::FreedMemoryGoesBackToTheSystem()
	{
		ReturnFreedMemory();
		const uint64_t before = HeldMemory();
		TEST_ASSERT(HeldWithBlocks() > before + c_Blocks * c_BlockBytes / 2);
		ReturnFreedMemory();
		TEST_ASSERT(HeldMemory() < before + c_Slack);
	}
#endif
} // namespace Testing
