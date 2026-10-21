// Auth (docs/11 "Authentication on the wire"): launch tokens, the device sessions behind the session cookie, the owner
// password and the login throttle, and the auth.* methods. Members run with the server lock held unless they say
// otherwise; a password is hashed outside it, one hash at a time.

include "devices.ch"
include "throttle.ch"
include "host/securitylog.ch"
include "web/rpc.ch"

namespace Ied
{
	const int64 kLaunchTokenMs = 60000;
	const int64 kMinPasswordChars = 12;

	enum class LoginOutcome { Ok, WrongPassword, NoPassword, Locked, Failed }

	class LoginResult
	{
		public LoginOutcome outcome;
		public Device* device;
		public Core::String cookie;
		public int64 retryAfterS;
	}

	class Auth
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// Reads auth/ in the data folder; false with `error` for a file there that is unreadable.
		public bool Load(Core::String& error);
		public bool HasPassword() const { return !passwordHash.IsEmpty(); }
		// A launch token that dies after `ttlMs`; empty when the system's random generator fails.
		public Core::String MintLaunchToken(int64 ttlMs);
		// Spends a live launch token on a new device session, its cookie value in `cookie`; null for a dead token.
		public Device* Exchange(Core::StringView token, Core::StringView deviceName, Core::String& cookie);
		// The device a cookie value belongs to, seen now; `refresh` when the cookie should be sent again.
		public Device* Authenticate(Core::StringView cookie, bool& refresh);
		// Without the server lock held: the throttle, then the password, then a device session.
		public LoginResult Login(Core::StringView address, Core::StringView password, Core::StringView deviceName);
		// The device session ends and its sockets are told bye and closed (the lock is released while they close).
		public void Logout(Device* device);
		public void Record(const char* event, Core::StringView fields);

	private:
		private Core::String SetPasswordMethod(Call& call, const Params& params);
		private Core::String DevicesMethod(Call& call, const Params& params);
		private Core::String RevokeMethod(Call& call, const Params& params);
		// Without the server lock held, holding `hashing`.
		private void ChangePassword(Core::StringView current, Core::StringView password);
		private LoginResult Finish(Core::StringView address, bool matched, Core::StringView deviceName);
		private bool SavePassword(Core::StringView hash);
		private void Disconnect(Core::StringView deviceId);

		private Server* server;
		private DeviceStore devices;
		private Core::HashMap<Core::String, int64> tokens;   // a live token's SHA-256, hex -> when it dies (ms)
		private Core::String passwordHash;
		private LoginThrottle throttle;
		private SecurityLog log;
		private Threading::Mutex hashing;   // a hash takes 64 MiB and a tenth of a second: one at a time
	}

	// Wall-clock and monotonic milliseconds.
	int64 UnixMs();
	int64 MonotonicMs();
}
