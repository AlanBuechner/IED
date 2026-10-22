// The models a chat can use (docs/04 "Providers", "Model groups and roles"): profiles from the ai.models setting, and a
// group's roles from ai.groups, each a model with its temperature, effort and fallbacks in order.

include "bedrock.ch"

namespace Ai
{
	enum class Privacy { Local, Lan, Cloud }

	const int32 kModelConnectMs = 10000;
	const int32 kModelPrefillBaseMs = 30000;
	const int32 kModelIdleLocalMs = 60000;
	const int32 kModelIdleCloudMs = 120000;
	const int64 kModelStepCapMs = 30 * 60 * 1000;
	const double kModelPrefillRate = 150.0;

	class ModelProfile
	{
		public Core::String name;          // its key in ai.models: what groups and chats name
		public Core::String endpoint;      // an OpenAI-compatible base URL ("http://127.0.0.1:8080/v1")
		public Core::String model;         // the id a request names; the profile's name when unset
		public int64 contextWindow;        // tokens; 0 when unknown
		public int64 maxOutput;            // tokens a reply may take; 0: the server's default
		public Privacy privacy;
		public bool vision;                // it is sent the images tool results show (images.ch), when `visionSet`
		public bool visionSet;             // the profile says; else its server does (llama.cpp's /props modalities)
		public Core::String apiKeyEnv;     // the environment variable holding the key (`"apiKey": {"env": ...}`)
		public Core::String effort;        // the request fields each effort adds (`"effort": {"low": {...}}`), as JSON; empty: by its server
		public int32 connectMs;
		public int32 prefillBaseMs;        // the prefill watchdog before the prompt's share (docs/12 "Watchdogs")
		public double prefillRate;         // prompt tokens per second assumed until one is measured
		public int32 idleMs;               // the idle watchdog between chunks
		public int64 stepCapMs;            // the hard cap on one request

		public Core::String CompletionsUrl() const;
	}

	class RoleModel
	{
		public Core::String model;
		public double temperature;
		public bool hasTemperature;
		public Core::String effort;
		public Core::Array<Core::String> fallback;
	}

	// The profiles in the ai.models value (JSON); one that cannot be used is named in `problems` instead.
	Core::Array<ModelProfile*> ReadModels(Core::StringView json, Core::Array<Core::String>& problems);
	ModelProfile* FindModel(const Core::Array<ModelProfile*>& models, Core::StringView name);
	// The role `role` of `group` in the ai.groups value; false when the group has no such role.
	bool ReadRole(Core::StringView groupsJson, Core::StringView group, Core::StringView role, RoleModel& into);
	// The models `role` of `group` tries in order - its model, then its fallbacks - of those `models` has; `chosen` gets
	// the role. A role the group leaves off is its primary; without a primary either, the first model alone.
	Core::Array<ModelProfile*> RoleCandidates(const Core::Array<ModelProfile*>& models, Core::StringView groupsJson, Core::StringView group, Core::StringView role, RoleModel& chosen);
	// The groups in the ai.groups value, by name.
	Core::Array<Core::String> GroupNames(Core::StringView groupsJson);
	const char* PrivacyName(Privacy privacy);
	// Whether `text` is an effort: low, medium or high.
	bool IsEffort(Core::StringView text);
}
