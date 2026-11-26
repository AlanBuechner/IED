#include "Serve/ServerWait.h"

namespace C4Compiler
{
	ServerWait JudgeWait(const WaitingServer& server)
	{
		if (!server.folderStays) return ServerWait::FolderGone;
		if (server.waitedSeconds >= server.idleSeconds) return ServerWait::Idle;
		if (server.firstBuildBytes != 0 && server.heldBytes > c_OutgrownFactor * server.firstBuildBytes) return ServerWait::Outgrown;
		if (server.waitedSeconds >= c_ShortMemoryIdleSeconds && server.machine && MachineNeedsMemory(*server.machine, server.heldBytes))
			return ServerWait::MemoryWanted;
		return ServerWait::Waiting;
	}
} // namespace C4Compiler
