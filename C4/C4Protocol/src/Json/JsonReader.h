#pragma once
#include "Json/JsonValue.h"
#include <string>

namespace Json
{
	/// Parses base-protocol framing (`Content-Length: N\r\n\r\n` + N-byte JSON body), as the LSP and DAP send it.
	/// Fed arbitrary chunks; hands back one complete document at a time, buffering partials. A frame it cannot
	/// use (no or an impossible length, a body that is not JSON) is dropped, and reading goes on with the next.
	class Reader
	{
	public:
		/// Feed a chunk of raw bytes into the internal buffer.
		void Append(const char* data, size_t length);

		/// The next complete document from the buffered input; false when no complete frame is left.
		bool TryReadDocument(Value& outDocument);

		/// Number of bytes currently held in the buffer (for diagnostics/tests).
		size_t BufferedBytes() const { return m_Buffer.size(); }

		/// No message is larger; a frame that claims more is dropped.
		static constexpr size_t kMaxContentLength = 64u << 20;

	private:
		static constexpr size_t npos = std::string::npos;

		/// Byte offset of the `\r\n\r\n` that terminates the header, or npos when absent.
		size_t FindHeaderEnd() const;

		/// The Content-Length value from the header bytes before `headerEnd`; false when there is none, it is
		/// not a number, or it is over kMaxContentLength.
		bool ParseContentLength(size_t headerEnd, size_t& outLength) const;

		static bool ParseLength(const std::string& digits, size_t& outLength);

		/// Strip leading/trailing whitespace from a header line.
		static std::string Trim(const std::string& value);

		std::string m_Buffer;
	};
} // namespace Json
