// SecurityLog (docs/11 "Audit"): logs/security.log, one JSON object per line, appended - starts, logins, device
// sessions, password changes. It never holds a secret.

include "bedrock.ch"

namespace Ied
{
	class SecurityLog
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Core::StringView file) { this.file = Core::String(file); }

		// {"ts": <unix ms>, "event": "<event>", ...`fields`}: `fields` is members as JSON text ("\"device\":\"dev-1\""),
		// or empty. False when the line could not be written.
		public bool Record(int64 unixMs, const char* event, Core::StringView fields);

		private Core::String file;
	}
}
