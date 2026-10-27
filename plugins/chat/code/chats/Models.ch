// The models a chat can use (docs/04 "Providers", "Model groups and roles"): profiles from the ai.models setting, and a
// group's roles from ai.groups, each a model with its temperature, effort and fallbacks in order.

include "bedrock.ch"

namespace Ai
{
	enum class Privacy { Local, Lan, Cloud }
	// What serves a profile: an OpenAI-compatible endpoint, or the owner's Claude Code CLI (docs/04 "The Claude Code provider").
	enum class Provider { OpenAi, ClaudeCode }

	const int32 c_ModelConnectMs = 10000;
	const int32 c_ModelPrefillBaseMs = 30000;
	const int32 c_ModelIdleLocalMs = 60000;
	const int32 c_ModelIdleCloudMs = 120000;
	const int64 c_ModelStepCapMs = 30 * 60 * 1000;
	const double c_ModelPrefillRate = 150.0;
	const int32 c_ClaudeFirstEventMs = 300000;   // a Claude step's wait for the CLI's first event (it retries the API itself)

	// Whether this build offers Claude through a Pro or Max login (docs/04 "Distributed builds"): the owner's own builds do;
	// a Dist build only once it defines CLAUDE_SUBSCRIPTION_APPROVED (Anthropic's approval of claude.ai login in IED).
#if defined(DIST) && !defined(CLAUDE_SUBSCRIPTION_APPROVED)
	const bool kClaudeSubscriptionOffered = false;
#else
	const bool c_ClaudeSubscriptionOffered = true;
#endif
	const char* c_ClaudeNotOffered = "Claude through a Pro or Max login is not in distributed builds of IED: Anthropic's terms do not let a third-party product offer claude.ai login without its approval (docs/04)";

	class ModelProfile
	{
		public Core::String m_Name;          // its key in ai.models: what groups and chats name
		public Provider m_Provider;
		public Core::String m_Endpoint;      // an OpenAI-compatible base URL ("http://127.0.0.1:8080/v1"); empty for Claude Code
		public Core::String m_Model;         // the id a request names; the profile's name when unset (Claude Code: "default")
		public int64 m_ContextWindow;        // tokens; 0 when unknown
		public int64 m_MaxOutput;            // tokens a reply may take; 0: the server's default
		public Privacy m_Privacy;
		public bool m_Vision;                // it is sent the images tool results show (Images.ch), when `visionSet`
		public bool m_VisionSet;             // the profile says; else its server does (llama.cpp's /props modalities)
		public bool m_KeepThinking;          // a llama.cpp server is sent the turn's earlier thinking (`keepThinking`, default true)
		public Core::String m_ApiKeyEnv;     // the environment variable holding the key (`"apiKey": {"env": ...}`)
		public Core::String m_Effort;        // the request fields each effort adds (`"effort": {"low": {...}}`), as JSON; empty: by its server
		public int32 m_ConnectMs;
		public int32 m_PrefillBaseMs;        // the prefill watchdog before the prompt's share (docs/12 "Watchdogs")
		public double m_PrefillRate;         // prompt tokens per second assumed until one is measured
		public int32 m_IdleMs;               // the idle watchdog between chunks
		public int64 m_StepCapMs;            // the hard cap on one request

		public Core::String CompletionsUrl() const;
		public bool IsClaude() const { return m_Provider == Provider::ClaudeCode; }
	}

	// When a Claude model's place in a chain is used (docs/04 "Pacing"): while the plan's pace against `target` (session, week,
	// both) is at most `max`; `max` 0 is always.
	struct PaceLimit
	{
		public double m_Max;
		public Core::String m_Target;
	}

	class RoleModel
	{
		public Core::String m_Model;
		public PaceLimit m_Pace;
		public double m_Temperature;
		public bool m_HasTemperature;
		public Core::String m_Effort;
		public Core::Array<Core::String> m_Fallback;
		public Core::Array<PaceLimit> m_FallbackPaces;   // each fallback's, in order

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
	// Sub-agent `agent` of `group` (its `agents`) as a role; false when the group has no such agent.
	bool ReadAgentRole(Core::StringView groupsJson, Core::StringView group, Core::StringView agent, RoleModel& into);
	// A role's JSON: {model, pace?, temperature?, effort?, fallback?}; false without a model.
	bool ReadRoleEntry(const Serialization::DomNode& entry, RoleModel& into);
	// The models `role` of `group` tries in order - its model, then its fallbacks - of those `models` has; `chosen` gets
	// the role. A role the group leaves off is its primary; without a primary either, the first model alone.
	Core::Array<ModelProfile*> RoleCandidates(const Core::Array<ModelProfile*>& models, Core::StringView groupsJson, Core::StringView group, Core::StringView role, RoleModel& chosen);
	// `candidates` with the profile `pinned` of `models` first and the rest after it in order; as they are when `pinned` is
	// empty or names no profile (docs/04 "Switching models").
	Core::Array<ModelProfile*> PinnedFirst(const Core::Array<ModelProfile*>& models, const Core::Array<ModelProfile*>& candidates, Core::StringView pinned);
	// The models a turn of a chat tries in order: a sub-agent chat's (`agent`) agent chain, else the role of `mode` in
	// `group`; `pinned` first either way.
	Core::Array<ModelProfile*> TurnCandidates(const Core::Array<ModelProfile*>& models, Core::StringView groupsJson, Core::StringView group, Core::StringView agent,
		Core::StringView mode, Core::StringView pinned, RoleModel& chosen);
	// The groups in the ai.groups value, by name.
	Core::Array<Core::String> GroupNames(Core::StringView groupsJson);
	const char* PrivacyName(Privacy privacy);
	// Whether `text` is an effort: low, medium, high, xhigh or max.
	bool IsEffort(Core::StringView text);
}
