#include "Target/OutputPipes.h"
#include <chrono>

namespace C4DAP::Target
{
	namespace
	{
		constexpr DWORD kReadBytes = 4096;
		// A program that started children of its own may leave a write end open; output stops being waited for
		// after this long.
		constexpr auto kDrainWait = std::chrono::milliseconds(2000);
		constexpr auto kDrainPoll = std::chrono::milliseconds(10);

		bool ContinuationByte(unsigned char c) { return (c & 0xC0) == 0x80; }

		size_t SequenceLength(unsigned char lead)
		{
			if (lead < 0x80) return 1;
			if ((lead & 0xE0) == 0xC0) return 2;
			if ((lead & 0xF0) == 0xE0) return 3;
			if ((lead & 0xF8) == 0xF0) return 4;
			return 1; // not a lead byte: pass it on as it is
		}

		bool OpenPipe(HANDLE& reader, HANDLE& writer)
		{
			SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
			if (!CreatePipe(&reader, &writer, &inheritable, 0)) return false;
			SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
			return true;
		}
	}

	size_t CompleteUtf8Prefix(const std::string& bytes)
	{
		size_t start = bytes.size();
		while (start > 0 && bytes.size() - start < 4 && ContinuationByte(static_cast<unsigned char>(bytes[start - 1]))) --start;
		if (start == 0) return bytes.size();
		const size_t lead = start - 1;
		return lead + SequenceLength(static_cast<unsigned char>(bytes[lead])) <= bytes.size() ? bytes.size() : lead;
	}

	OutputPipes::~OutputPipes()
	{
		Finish();
		for (Pipe* pipe : { &m_Out, &m_Err })
		{
			if (pipe->writer) CloseHandle(pipe->writer);
			if (pipe->reader) CloseHandle(pipe->reader);
		}
	}

	bool OutputPipes::Open()
	{
		return OpenPipe(m_Out.reader, m_Out.writer) && OpenPipe(m_Err.reader, m_Err.writer);
	}

	void OutputPipes::StartForwarding(Sink sink)
	{
		for (Pipe* pipe : { &m_Out, &m_Err })
		{
			CloseHandle(pipe->writer);
			pipe->writer = nullptr;
		}
		m_Out.forwarder = std::thread(Forward, m_Out.reader, "stdout", sink);
		m_Err.forwarder = std::thread(Forward, m_Err.reader, "stderr", sink);
	}

	void OutputPipes::Forward(HANDLE reader, const char* category, Sink sink)
	{
		std::string pending;
		char buffer[kReadBytes];
		DWORD got = 0;
		while (ReadFile(reader, buffer, kReadBytes, &got, nullptr) && got > 0)
		{
			pending.append(buffer, got);
			const size_t complete = CompleteUtf8Prefix(pending);
			if (complete == 0) continue;
			sink(category, pending.substr(0, complete));
			pending.erase(0, complete);
		}
		if (!pending.empty()) sink(category, pending);
	}

	void OutputPipes::Join(Pipe& pipe)
	{
		if (!pipe.forwarder.joinable()) return;
		// The read ends when the program's last write end closes; one still held elsewhere is cancelled.
		const auto deadline = std::chrono::steady_clock::now() + kDrainWait;
		DWORD available = 0;
		while (std::chrono::steady_clock::now() < deadline && PeekNamedPipe(pipe.reader, nullptr, 0, nullptr, &available, nullptr))
			std::this_thread::sleep_for(kDrainPoll);
		CancelSynchronousIo(pipe.forwarder.native_handle());
		pipe.forwarder.join();
	}

	void OutputPipes::Finish()
	{
		Join(m_Out);
		Join(m_Err);
	}
} // namespace C4DAP::Target
