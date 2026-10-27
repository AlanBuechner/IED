// Devices (docs/11): the device sessions behind session cookies, kept in auth/devices.json. A cookie value is 32
// random bytes as hex; the file keeps only its SHA-256, so nothing in it is a credential.

include "bedrock.ch"

namespace Ied
{
	const int64 c_DeviceLifetimeMs = 2592000000;   // 30 days after the device was last seen
	const int64 c_CookieRefreshMs = 86400000;      // the cookie (and the file's lastSeen) follow at most once a day

	enum class DeviceKind { Token, Password }

	class Device
	{
		public Core::String m_Id;
		public Core::String m_Name;
		public DeviceKind m_Kind;
		public int64 m_CreatedMs;
		public int64 m_LastSeenMs;
		public int64 m_RefreshedMs;          // when the cookie was last sent; not kept in the file
		public Core::String m_SecretDigest;  // the SHA-256 of the cookie value, hex

		// {"id", "name", "kind", "created", "lastSeen"}
		public Core::String ToJson() const;
	}

	class DeviceStore
	{
		[operator("create")] public void Init() {}

		// The devices `file` holds; false with `error` for a file that is not a devices file. No file is no devices.
		public bool Load(Core::StringView file, int64 nowMs, Core::String& error);
		// A new device session; its cookie value in `cookie`. Null when the system's random generator fails.
		public Device* Create(Core::StringView name, DeviceKind kind, int64 nowMs, Core::String& cookie);
		// The device a cookie value belongs to, seen now; null for an unknown or expired one (an expired one is removed).
		// `refresh` says the cookie should be sent again: a day has passed since it last was.
		public Device* Find(Core::StringView cookie, int64 nowMs, bool& refresh);
		public Device* ById(Core::StringView id) const;
		public void Remove(Device* device);
		public const Core::Array<Device*>& All() const { return m_Devices; }
		// Writes the file whole; false when it could not.
		public bool Save() const;

		private Core::String m_File;
		private Core::Array<Device*> m_Devices;
	}

	// 32 random bytes as 64 hex digits; empty when the system's generator fails.
	Core::String NewSecret();
	// The SHA-256 of a secret, hex.
	Core::String SecretDigest(Core::StringView secret);
	const char* DeviceKindName(DeviceKind kind);
}
