// testhooks.*: the deterministic control surface of the protocol conformance suite (tools/conformance/README.md):
// test topics and event floods, streams, delays, counters, chosen failures, a simulated restart, scratch files and
// server-side document edits, a file outside the workspace as a language server would name one, and a full garbage
// collection with its counts (the idle soak's leak measure). Only a server started with --test-hooks has them.

include "web/Rpc.ch"

namespace Ied
{
	class TestTopic
	{
		public Core::String m_Key;
		public int64 m_Events;
		public int64 m_TextLength;
	}

	class TestHooks
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The lock is held: every stream stops.
		public void OnRestart();

	private:
		private TestTopic* TopicParam(const Params& params) const;
		private void Publish(TestTopic* topic, Core::StringView kind, int64 size);
		private void RunStream(TestTopic* topic, int64 count, int64 intervalMs, int64 size, CancelToken* token);
		private Core::String Configure(Call& call, const Params& params);
		private Core::String NewTopic(Call& call, const Params& params);
		private Core::String Emit(Call& call, const Params& params);
		private Core::String Stream(Call& call, const Params& params);
		private Core::String Delay(Call& call, const Params& params);
		private Core::String Count(Call& call, const Params& params);
		private Core::String Fail(Call& call, const Params& params);
		private Core::String Restart(Call& call, const Params& params);
		private Core::String Scratch(Call& call, const Params& params);
		private Core::String ServerEdit(Call& call, const Params& params);
		private Core::String External(Call& call, const Params& params);
		private Core::String Info(Call& call, const Params& params);
		private Core::String CollectGarbage(Call& call, const Params& params);
		private Core::String Crash(Call& call, const Params& params);
		private Core::String AskRelaunch(Call& call, const Params& params);
		private Core::String MintToken(Call& call, const Params& params);
		private void RegisterMethods();

		private Server* m_Server;
		private Core::HashMap<Core::String, TestTopic*> m_Topics;
		private Core::HashMap<Core::String, int64> m_Counters;
		private Core::Array<CancelToken*> m_Streams;
		private int64 m_ScratchFiles;
		private int64 m_Delays;
	}
}
