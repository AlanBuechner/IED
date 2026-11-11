// The default chain (docs/04 "Which model a turn uses"; owner, 2026-11-11: "i dont realy use the model groups so we can
// get rid of that"): what a chat that names no model runs on, and what a chosen model falls back on. It is
// ai.defaultModel - empty: Claude's ladder when Claude is set up (Opus, then Sonnet, Haiku and Fable while the plan runs
// ahead of its pace, ai.claude.stepDown), else the first model - then ai.fallbackModel (empty: behind a Claude chain, the
// first model that is not Claude's; "none": nothing). With the sub-agents of ai.agents it is written as the one group the
// routing, the pace and the sub-agents read ({"default": {"primary": <role>, "agents": <ai.agents>}}), so they run as
// they did when that was a group of the settings.

include "api/Plugin.ch"
include "Models.ch"

namespace Ai
{
	// The name the routing knows the default chain by (a turn's `group`).
	const char* c_DefaultChain = "default";

	// The default chain and the sub-agents as the routing reads them, for workspace `wsId` ("": the app's).
	Core::String DefaultGroupsJson(const IED::PluginContext* context, Core::StringView wsId);
	// The same from the settings' values, each as its JSON text (ai.models, ai.defaultModel, ai.fallbackModel,
	// ai.claude.stepDown, ai.agents): for the tests and for DefaultGroupsJson.
	Core::String DefaultGroupsFrom(Core::StringView modelsJson, Core::StringView defaultModelJson, Core::StringView fallbackModelJson, Core::StringView stepDownJson, Core::StringView agentsJson);
}
