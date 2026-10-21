// The Vulkan replay (docs/22 "Replay").
#include "replay/replay.h"

namespace fb::replay
{
	std::unique_ptr<ApiReplay> MakeVulkanReplay() {
		Fail("this replayer does not read Vulkan captures yet");
	}
}
