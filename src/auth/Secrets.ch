// Secrets (docs/11 "Secrets"): the master's secret store (auth/secrets.json in the data folder) and the secrets.* methods,
// through which a value goes in and never comes out. A change is in the security log by name, is told on the server
// topic (`secrets.changed`) and wakes the config server mirror, whose token may be one (docs/35). Methods run with the
// server lock held.

include "SecretStore.ch"
include "web/Rpc.ch"

namespace Ied { class Server; }

namespace Ied
{
	class Secrets
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public SecretStore* Store() const { return m_Store; }

	private:
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String SetMethod(Call& call, const Params& params);
		private Core::String RemoveMethod(Call& call, const Params& params);
		private void Changed(const char* event, Core::StringView name);

		private Server* m_Server;
		private SecretStore* m_Store;
	}
}
