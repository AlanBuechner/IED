#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <thread>

namespace C4DAP::Target
{
	/// The longest prefix of `bytes` that does not end inside a UTF-8 sequence (the rest waits for the next read).
	size_t CompleteUtf8Prefix(const std::string& bytes);

	/// The program's stdout and stderr: pipes whose write ends the program inherits, read by two threads that
	/// hand each piece of text to the sink as "stdout" or "stderr" output.
	class OutputPipes
	{
	public:
		using Sink = std::function<void(const char* category, const std::string& text)>;

		OutputPipes() = default;
		OutputPipes(const OutputPipes&) = delete;
		OutputPipes& operator=(const OutputPipes&) = delete;
		~OutputPipes();

		bool Open();
		HANDLE StdoutWriter() const { return m_Out.writer; }
		HANDLE StderrWriter() const { return m_Err.writer; }
		/// The program holds its own copies of the write ends now: closes ours and starts reading.
		void StartForwarding(Sink sink);
		/// Waits until the program's last output is forwarded (it has ended), for a bounded time.
		void Finish();

	private:
		struct Pipe
		{
			HANDLE reader = nullptr;
			HANDLE writer = nullptr;
			std::thread forwarder;
		};
		static void Forward(HANDLE reader, const char* category, Sink sink);
		static void Join(Pipe& pipe);

		Pipe m_Out;
		Pipe m_Err;
	};
} // namespace C4DAP::Target
