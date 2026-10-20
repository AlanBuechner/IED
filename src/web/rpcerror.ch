// RpcError: how a method fails (docs/10): one of the stable error codes, a message, and optional data. Methods throw
// it; the session answers the request with it.
//     throw Ied::RpcError(Ied::ErrorCode::NotFound, "no document d7");

include "bedrock.ch"

namespace Ied
{
	enum class ErrorCode { BadRequest, NotFound, Forbidden, Untrusted, Conflict, Busy, Cancelled, Timeout, Unavailable, Internal, PluginError }

	const int32 kErrorCodeCount = 11;

	// "bad_request", "not_found", ...
	const char* ErrorCodeName(ErrorCode code);
	// The code a name stands for; false for anything else.
	bool ParseErrorCode(Core::StringView name, ErrorCode& code);

	class RpcError
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(ErrorCode code, Core::StringView message) { this.code = code; this.message = Core::String(message); }
		[operator("create")] public void Init(ErrorCode code, const char* message) { this.code = code; this.message = Core::String(message); }
		// With `data`, JSON text.
		[operator("create")] public void Init(ErrorCode code, const char* message, Core::StringView data) {
			this.code = code;
			this.message = Core::String(message);
			this.data = Core::String(data);
		}
		[operator("create")] public void Init(ErrorCode code, Core::StringView message, Core::StringView data) {
			this.code = code;
			this.message = Core::String(message);
			this.data = Core::String(data);
		}

		// {"code": ..., "message": ..., "data": ...}
		public Core::String ToJson() const;

		public ErrorCode code;
		public Core::String message;
		public Core::String data;   // JSON text; empty when there is none
	}
}
