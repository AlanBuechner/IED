// The models a chat can use (docs/04 "Providers", "Model groups and roles"): profiles from the ai.models setting, and a
// group's roles from ai.groups, each a model with its temperature, effort and fallbacks in order.

include "bedrock.ch"

namespace Ai
{
	enum class Privacy { Local, Lan, Cloud }
	// What serves a profile: an OpenAI-compatible endpoint, or the owner's Claude Code CLI (docs/04 "The Claude Code provider").
	enum class Provider { OpenAi, ClaudeCode }

	const int32 kModelConnectMs = 10000;
	const int32 kModelPrefillBaseMs = 30000;
	const int32 kModelIdleLocalMs = 60000;
	const int32 kModelIdleCloudMs = 120000;
	const int64 kModelStepCapMs = 30 * 60 * 1000;
	const double kModelPrefillRate = 150.0;
	const int32 kClaudeFirstEventMs = 300000;   // a Claude step's wait for the CLI's first event (it retries the API itself)

	// Whether this build offers Claude through a Pro or Max login (docs/04 "Distributed builds"): the owner's own builds do;
	// a Dist build only once it defines CLAUDE_SUBSCRIPTION_APPROVED (Anthropic's approval of claude.ai login in IED).
#if defined(DIST) && !defined(CLAUDE_SUBSCRIPTION_APPROVED)
	const bool kClaudeSubscriptionOffered = false;
#else
	const bool kClaudeSubscriptionOffered = true;
#endif
	const char* kClaudeNotOffered = "Claude through a Pro or Max login is not in distributed builds of IED: Anthropic's terms do not let a third-party product offer claude.ai login without its approval (docs/04)";

	class ModelProfile
	{
		public Core::String name;          // its key in ai.models: what groups and chats name
		public Provider provider;
		public Core::String endpoint;      // an OpenAI-compatible base URL ("http://127.0.0.1:8080/v1"); empty for Claude Code
		public Core::String model;         // the id a request names; the profile's name when unset (Claude Code: "default")
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
		public bool IsClaude() const { return provider == Provider::ClaudeCode; }
	}

	// When a Claude model's place in a chain is used (docs/04 "Pacing"): while the plan's pace against `target` (session, week,
	// both) is at most `max`; `max` 0 is always.
	struct PaceLimit
	{
		public double max;
		public Core::String target;
	}

	class RoleModel
	{
		public Core::String model;
		public PaceLimit pace;
		public double temperature;
		public bool hasTemperature;
		public Core::String effort;
		public Core::Array<Core::String> fallback;
		public Core::Array<PaceLimit> fallbackPaces;   // each fallback's, in order

		// The pace of `name`'s place in the chain; `max` 0 when it has none.
		public PaceLimit PaceOf(Core::StringView name) const;
	}

	// The profiles in the ai.models value (JSON); one that cannot be used is named in `problems` instead.
	Core::Array<ModelProfile*> ReadModels(Core::StringView json, Core::Array<Core::String>& problems);
	ModelProfile* FindModel(const Core::Array<ModelProfile*>& models, Core::StringView name);
	// The role a turn in `mode` uses (docs/04 "Which role a turn uses"): ask primary, plan planning, agent implementation.
	Core::StringView RoleOfMode(Core::StringView mode);
	// A place's pace: `{"max": 1.25, "target": "week"}` (target both when it names none); `max` 0 when there is none.
	PaceLimit ReadPace(const Serialization::DomNode& node);
	// The role `role` of `group` in the ai.groups value; false when the group has no such role. A fallback is a model's name or
	// {model, pace}.
	bool ReadRole(Core::StringView groupsJson, Core::StringView group, Core::StringView role, RoleModel& into);
	// The models `role` of `group` tries in order - its model, then its fallbacks - of those `models` has; `chosen` gets
	// the role. A role the group leaves off is its primary; without a primary either, the first model alone.
	Core::Array<ModelProfile*> RoleCandidates(const Core::Array<ModelProfile*>& models, Core::StringView groupsJson, Core::StringView group, Core::StringView role, RoleModel& chosen);
	// `candidates` with the profile `pinned` of `models` first and the rest after it in order; as they are when `pinned` is
	// empty or names no profile (docs/04 "Switching models").
	Core::Array<ModelProfile*> PinnedFirst(const Core::Array<ModelProfile*>& models, const Core::Array<ModelProfile*>& candidates, Core::StringView pinned);
	// The groups in the ai.groups value, by name.
	Core::Array<Core::String> GroupNames(Core::StringView groupsJson);
	const char* PrivacyName(Privacy privacy);
	// Whether `text` is an effort: low, medium or high.
	bool IsEffort(Core::StringView text);
}
