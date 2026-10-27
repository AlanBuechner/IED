// Requests (docs/10): methods registered by name run with the server lock held, like the mock's single thread, and
// release it only to wait (Call::Sleep). A method returns its result as JSON text or throws RpcError. A request with an
// opId runs once per client and opId for 10 minutes: a retry gets the first answer, waiting for it if it is still
// running (decision 7).
//     server.rpc.Register("fs.read", [this](Ied::Call& call, const Ied::Params& params) -> Core::String { ... });

include "Params.ch"
include "Hub.ch"

namespace Ied
{
	// A request's cancellation (`cancel`): set once, waking a method that sleeps.
	class CancelToken
	{
		public bool IsCancelled() const { return m_Cancelled.Load(); }
		// With the server lock held.
		public void Cancel() { m_Cancelled.Store(true); m_Wake.NotifyAll(); }

		public Threading::Atomic<bool> m_Cancelled;
		public Threading::ConditionVariable m_Wake;
	}

	// What a method sees: the server (its lock is held), the calling session, the request id and opId, and its
	// cancellation.
	class Call
	{
		public Server* m_Server;
		public Session* m_Session;
		public int64 m_RequestId;
		public Core::String m_OpId;
		public CancelToken* m_Cancel;

		public Core::StringView ClientId() const;
		// Waits `ms` with the server lock released; throws RpcError cancelled when the request is cancelled.
		public void Sleep(int64 ms);
		// A stream this request started (testhooks.stream): `cancel` with the request's id stops it through `token`.
		public void OpenStream(CancelToken* token);
	}

	alias Method = func<Core::String(Call&, const Params&)>;

	// A request's answer: its result or error as JSON text.
	struct Outcome
	{
		public bool m_Ok;
		public Core::String m_Json;
	}

	class OpEntry
	{
		public Core::String m_Method;
		public bool m_Done;
		public Outcome m_Outcome;
		public int64 m_CreatedNs;
	}

	const int64 c_OpLifetimeNs = 600000000000;

	class Rpc
	{
		public void Register(const char* name, Method method) { m_Methods.Insert(Core::String(name), method); }
		// A plugin's method goes with its code (docs/26 "Built-ins on the API").
		public void Unregister(Core::StringView name) { m_Methods.Remove(Core::String(name)); }
		public bool Has(Core::StringView name) const { return m_Methods.Contains(Core::String(name)); }
		public Core::Array<Core::String> Names() const;
		// Runs a request (the server lock is held), once per opId.
		public Outcome Run(Call& call, Core::StringView method, const Params& params);
		// A restart forgets every opId.
		public void ClearOps() { m_Ops.Clear(); }

	private:
		private Outcome Invoke(Call& call, Core::StringView method, const Params& params);
		private Outcome RunOnce(Call& call, Core::StringView method, const Params& params);
		private void ExpireOps(int64 nowNs);

		private Core::HashMap<Core::String, Method> m_Methods;
		private Core::HashMap<Core::String, OpEntry*> m_Ops;   // "<clientId> <opId>"
	}

	Outcome Failure(const RpcError& error);
	Outcome Success(Core::StringView json);
}
