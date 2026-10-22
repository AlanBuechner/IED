// The plan's Claude usage (docs/04 "Usage"): one record for the server - the windows from the CLI's get_usage answers,
// asked through a chat's session when its turn ends or through a short CLI process of IED's own, and from the
// rate_limit_events sessions stream - with the CLI found, its version and whether it is signed in (`claude auth status`);
// the app-wide topic aiusage and the method ai.usage (docs/10).

include "api/plugin.ch"
include "claudewire.ch"
include "claudecli.ch"
include "models.ch"

namespace Ai
{
	const char* kUsageTopic = "aiusage";
	const int64 kUsageFreshMs = 20000;        // a refresh asked this soon after an answer is answered from it
	const int32 kUsageProcessMs = 45000;

	class ClaudeAccount
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		// Asks the CLI now - its version, whether it is signed in, the plan's usage - by a process of its own, unless an
		// answer is fresher than kUsageFreshMs and `force` is false. Any thread, without the host lock.
		public void Refresh(bool force);
		// Inside the host lock: a session's get_usage answer, a rate_limit_event's rate_limit_info.
		public void TookUsage(const Serialization::DomNode& response);
		public void TookRateLimit(const Serialization::DomNode& info);
		// Inside the host lock: what `claude auth status` said as a session started.
		public void TookAuth(bool signedIn, Core::StringView method);
		// Inside the host lock: a turn met a usage limit; Claude is used up until `untilMs` (0: until the plan says).
		public void MetLimit(int64 untilMs);
		// Inside the host lock: when Claude's shared windows are used up until, or 0 when they are not.
		public int64 BlockedUntil(int64 nowMs) const;
		// {claude: {configured, cli: {path, version, signIn, authMethod, subscription}, refreshing, blockedUntilMs, nowMs,
		// paced: [{profile, target, ladder, model, why, untilMs}], usage: WriteUsage}}
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

		private void Changed();
		private Core::String UsageMethod(const IED::Request& request);

		private IED::PluginContext* context;
	}
}
