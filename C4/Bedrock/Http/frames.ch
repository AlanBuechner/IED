// WebSocket frames (D-67, RFC 6455 section 5): read and written as bytes, never touching a socket. A client's frames
// are masked and a server's are not; no extension is negotiated, so the reserved bits are always 0.

include "parse.ch"

namespace Http
{
	enum class Opcode { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 }

	// Close codes (RFC 6455 section 7.4.1).
	const int32 kCloseNormal = 1000;
	const int32 kCloseGoingAway = 1001;
	const int32 kCloseProtocolError = 1002;
	const int32 kCloseUnsupportedData = 1003;
	const int32 kCloseNoCode = 1005;          // received only: a close frame without a code
	const int32 kCloseAbnormal = 1006;        // never sent: the connection ended without a close frame
	const int32 kCloseInvalidPayload = 1007;  // a text message that is not UTF-8
	const int32 kClosePolicy = 1008;
	const int32 kCloseTooBig = 1009;
	const int32 kCloseInternalError = 1011;

	// The largest payload a control frame (close, ping, pong) may carry.
	const int64 kMaxControlPayload = 125;

	struct Frame
	{
		public bool fin;
		public Opcode opcode;
		public Core::Array<uint8> payload;   // unmasked
	}

	// One frame read: Complete with its length in bytes, Incomplete, TooLarge (payload over the limit), or Malformed
	// with why. `closeCode` is what to close the connection with when it failed: 1002 or 1009.
	struct FrameParse
	{
		public ParseStatus status;
		public int64 length;
		public Core::String error;
		public int32 closeCode;
	}

	// The frame at the start of `bytes` into `frame`. `fromClient`: a client's frames must be masked, a server's must
	// not be. Lengths must use their shortest form; a control frame must be whole and carry at most 125 bytes.
	FrameParse ParseFrame(Core::StringView bytes, bool fromClient, int64 maxPayload, Frame& frame);

	// Appends a frame to `out`: masked with `mask` (four bytes) when it is not null, as a client's must be.
	void WriteFrame(Core::Array<uint8>& out, bool fin, Opcode opcode, const void* payload, int64 count, const uint8* mask);

	// A close frame's payload: its code (kCloseNoCode when it has none) and reason. False when it is malformed - one
	// byte, a code no endpoint may send, or a reason that is not UTF-8.
	bool ReadClosePayload(const Core::Array<uint8>& payload, int32& code, Core::String& reason);
	// A close frame's payload for `code` and `reason` (cut to fit 125 bytes at a whole character).
	Core::Array<uint8> ClosePayload(int32 code, Core::StringView reason);
}
