// MasterLink (a workspace server, docs/01 "Processes", docs/10 "Local control channel"): requests to the master's
// control pipe - what changed of the workspace, app-tier writes, trust, bindings the master answers, a relaunch. One
// connection per request, so a request never waits behind another's answer.

include "bedrock.ch"

namespace Ied
{
	const int32 kMasterAnswerMs = 30000;

	class MasterLink
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Core::StringView control) { this.control = Core::String(control); }

		// `method` with `paramsJson`: its result as JSON text, or false with `problem`. `held`, when not null, is a lock
		// the caller holds, released while the master answers. Waits at most `timeoutMs` for the answer.
		public bool Call(Threading::Mutex* held, Core::StringView method, Core::StringView paramsJson, Core::String& result, Core::String& problem, int32 timeoutMs = kMasterAnswerMs);
		// The same; a problem is thrown as RpcError unavailable.
		public Core::String Ask(Threading::Mutex* held, Core::StringView method, Core::StringView paramsJson);
		public Core::String Control() const { return control; }

		private Core::String control;
	}
}
