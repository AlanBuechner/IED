// testhooks.*: the deterministic control surface of the protocol conformance suite (tools/conformance/README.md):
// test topics and event floods, streams, delays, counters, chosen failures, a simulated restart, scratch files and
// server-side document edits, a file outside the workspace as a language server would name one. Only a server started
// with --test-hooks has them.

include "web/rpc.ch"

namespace Ied
{
	class TestTopic
	{
		public Core::String key;
		public int64 events;
		public int64 textLength;
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
		private Core::String MintToken(Call& call, const Params& params);
		private void RegisterMethods();

		private Server* server;
		private Core::HashMap<Core::String, TestTopic*> topics;
		private Core::HashMap<Core::String, int64> counters;
		private Core::Array<CancelToken*> streams;
		private int64 scratchFiles;
		private int64 delays;
	}
}
