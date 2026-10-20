#pragma once
#include "Json/JsonReader.h"
#include <mutex>
#include <string>

namespace Json
{
	/// The process's stdin and stdout as a framed JSON channel (the LSP and DAP servers' wire). The handles are
	/// taken once and made non-inheritable, so a process the server starts (a debuggee) never shares them.
	class StdioChannel
	{
	public:
		StdioChannel();

		/// The next complete document; false at the end of the input.
		bool Read(Value& document);

		/// One framed message (Writer::Encode); safe to call from any thread.
		void Write(const std::string& framed);

	private:
		bool ReadChunk();

		void* m_In = nullptr;
		void* m_Out = nullptr;
		Reader m_Reader;
		std::mutex m_WriteLock;
	};
} // namespace Json
