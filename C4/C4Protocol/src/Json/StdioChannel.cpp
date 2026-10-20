#include "Json/StdioChannel.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Json
{
	namespace
	{
		constexpr size_t kChunkBytes = 8192;
	}

	// The OS handles are used directly so no text-mode newline translation happens: the framing carries exact
	// \r\n header bytes and byte-accurate lengths.
	StdioChannel::StdioChannel()
	{
#ifdef _WIN32
		m_In = GetStdHandle(STD_INPUT_HANDLE);
		m_Out = GetStdHandle(STD_OUTPUT_HANDLE);
		SetHandleInformation(m_In, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(m_Out, HANDLE_FLAG_INHERIT, 0);
#endif
	}

	bool StdioChannel::Read(Value& document)
	{
		while (!m_Reader.TryReadDocument(document))
			if (!ReadChunk())
				return false;
		return true;
	}

	bool StdioChannel::ReadChunk()
	{
		char buffer[kChunkBytes];
#ifdef _WIN32
		DWORD got = 0;
		if (!ReadFile(m_In, buffer, static_cast<DWORD>(sizeof(buffer)), &got, nullptr) || got == 0)
			return false;
		m_Reader.Append(buffer, got);
#else
		const ssize_t got = ::read(0, buffer, sizeof(buffer));
		if (got <= 0)
			return false;
		m_Reader.Append(buffer, static_cast<size_t>(got));
#endif
		return true;
	}

	void StdioChannel::Write(const std::string& framed)
	{
		std::lock_guard<std::mutex> lock(m_WriteLock);
#ifdef _WIN32
		size_t sent = 0;
		while (sent < framed.size())
		{
			DWORD written = 0;
			if (!WriteFile(m_Out, framed.data() + sent, static_cast<DWORD>(framed.size() - sent), &written, nullptr) || written == 0)
				return;
			sent += written;
		}
#else
		size_t sent = 0;
		while (sent < framed.size())
		{
			const ssize_t written = ::write(1, framed.data() + sent, framed.size() - sent);
			if (written <= 0)
				return;
			sent += static_cast<size_t>(written);
		}
#endif
	}
} // namespace Json
