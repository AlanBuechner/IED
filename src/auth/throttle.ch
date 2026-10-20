// LoginThrottle (docs/11): five failed logins from one address within a minute lock it out for a minute, doubling with
// each lockout in a row up to an hour; a success forgets the address.

include "bedrock.ch"

namespace Ied
{
	const int32 kFailuresBeforeLockout = 5;
	const int64 kFailureWindowMs = 60000;
	const int64 kFirstLockoutMs = 60000;
	const int64 kLongestLockoutMs = 3600000;

	class LoginAttempts
	{
		public Core::Array<int64> failures;   // within the window, oldest first
		public int64 lockedUntilMs;
		public int32 lockouts;                // in a row
		public int64 lastFailureMs;
	}

	class LoginThrottle
	{
		// 0 when `address` may try now, else the whole seconds until it may.
		public int64 RetryAfterS(Core::StringView address, int64 nowMs) const;
		// A failed login; true when it locked the address out.
		public bool Failed(Core::StringView address, int64 nowMs);
		public void Succeeded(Core::StringView address);

		private Core::HashMap<Core::String, LoginAttempts*> attempts;
	}
}
