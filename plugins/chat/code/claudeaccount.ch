// The plan's Claude usage (docs/04 "Usage"): one record for the server - the windows from the CLI's get_usage answers,
// asked through a chat's session when its turn ends or through a short CLI process of IED's own, and from the
// rate_limit_events sessions stream - with the CLI found, its version and whether it is signed in (`claude auth status`);
// the app-wide topic aiusage and the method ai.usage (docs/10). And setting Claude up (docs/04 "Setting it up"): the CLI's
// own install and sign-in windows opened and watched (ai.claude.install, ai.claude.signIn), and the Claude models added to
// the app settings once the CLI is signed in to a subscription (ai.claude.setup, and by itself once).

include "api/plugin.ch"
include "claudewire.ch"
include "claudecli.ch"
include "models.ch"

namespace Ai
{
	const char* kUsageTopic = "aiusage";
	const int64 kUsageFreshMs = 20000;        // a refresh asked this soon after an answer is answered from it
	const int32 kUsageProcessMs = 45000;
	const int64 kClaudeWindowWatchMs = 15 * 60 * 1000;   // how long a sign-in or install window is watched
	const int64 kClaudeWindowPollNs = 2000000000;

	class ClaudeAccount
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		// Asks the CLI now - its version, whether it is signed in, the plan's usage - by a process of its own, unless an
		// answer is fresher than kUsageFreshMs and `force` is false; a CLI signed in to a subscription with no Claude model
		// set up gets one (Setup). Any thread, without the host lock.
		public void Refresh(bool force);
		// Adds the Claude profiles and the claude group to the app settings (WithClaudeModels): once - a mark in the plugin's
		// state folder - unless `force` (Add Claude models, a sign-in). True when it wrote them; false with `problem` when it
		// could not, and with none when there was nothing to do. Any thread, without the host lock.
		public bool Setup(bool force, Core::String& problem);
		// The first pacing design's `model: "auto"` profiles in the app settings rewritten into ai.groups (WithoutAutoProfiles).
		// Any thread, without the host lock.
		public void MigrateAuto();
		// Inside the host lock: a session's get_usage answer, a rate_limit_event's rate_limit_info.
		public void TookUsage(const Serialization::DomNode& response);
		public void TookRateLimit(const Serialization::DomNode& info);
		// Inside the host lock: what `claude auth status` said as a session started.
		public void TookAuth(bool signedIn, Core::StringView method);
		// Inside the host lock: a turn met a usage limit; Claude is used up until `untilMs` (0: until the plan says).
		public void MetLimit(int64 untilMs);
		// Inside the host lock: when Claude's shared windows are used up until, or 0 when they are not.
		public int64 BlockedUntil(int64 nowMs) const;
		// {claude: {offered, configured, cli: {asked, path, version, signIn, authMethod, subscription, signingIn, installing},
		// refreshing, blockedUntilMs, nowMs, chains: WriteChains, usage: WriteUsage}}
		public Core::String SnapshotJson();
		// Whether ai.models names a claude-code profile.
		public bool Configured() const;

		public ClaudeUsage usage;            // host lock, as the rest
		public Core::String cliPath;         // the CLI found; empty: none
		public Core::String cliVersion;
		public Core::String signIn;          // signed_in, signed_out; empty before the CLI was asked
		public Core::String authMethod;      // as `claude auth status` says (claude.ai, api_key, ...)
		public int64 blockedUntilMs;         // a usage limit a turn met
		public bool refreshing;
		public bool asked;                   // the CLI looked for and asked at least once since the server started
		public bool signingIn;               // a sign-in window is open and watched
		public bool installing;              // an install window is open and watched

		private void Changed();
		private Core::Array<Core::String> Command() const;
		private Core::String UsageMethod(const IED::Request& request);
		// ai.claude.signIn: the CLI's own subscription login (`auth login --claudeai`) in a window of its own, watched until
		// `auth status` says claude.ai; then Claude is set up if it is not.
		private Core::String SignInMethod(const IED::Request& request);
		// ai.claude.install: the official installer in a PowerShell window of its own, watched until the CLI is found.
		private Core::String InstallMethod(const IED::Request& request);
		// ai.claude.setup: Add Claude models (Setup forced).
		private Core::String SetupMethod(const IED::Request& request);
		// On a thread of the plugin's: `done` asked every kClaudeWindowPollNs until it says true or kClaudeWindowWatchMs
		// pass, then `after` with whether it did.
		private void Watch(func<bool()> done, func<void(bool)> after);
		// `waiting` (signingIn, installing) set unless it already was: false when another window is open for it.
		private bool Claim(bool& waiting);
		private void Release(bool& waiting);

		private IED::PluginContext* context;
	}
}
