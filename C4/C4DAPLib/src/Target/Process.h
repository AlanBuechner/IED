#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace C4DAP::Target
{
	/// The debuggee's process and threads, as its debug events introduce them, and access to its memory and
	/// registers. The handles belong to the debug session (the system closes them when it ends).
	class Process
	{
	public:
		void Started(const CREATE_PROCESS_DEBUG_INFO& info, DWORD processId, DWORD threadId);
		void ThreadStarted(DWORD threadId, HANDLE thread);
		void ThreadEnded(DWORD threadId);
		void Ended();

		bool Alive() const { return m_Handle != nullptr; }
		HANDLE Handle() const { return m_Handle; }
		DWORD Id() const { return m_Id; }
		DWORD MainThread() const { return m_Threads.empty() ? 0 : m_Threads.front().first; }
		HANDLE Thread(DWORD threadId) const;
		std::vector<DWORD> Threads() const;

		bool Read(uint64_t address, void* into, size_t size) const;
		template <typename T> bool Read(uint64_t address, T& value) const { return Read(address, &value, sizeof(T)); }
		/// Writes code or data; the instruction cache is flushed so a patched instruction takes effect.
		bool Write(uint64_t address, const void* from, size_t size) const;
		/// A NUL-terminated string of at most `limit` bytes; `complete` says whether the NUL was found.
		std::string ReadText(uint64_t address, size_t limit, bool& complete) const;

		bool Registers(DWORD threadId, CONTEXT& context) const;
		bool SetRegisters(DWORD threadId, const CONTEXT& context) const;

	private:
		HANDLE m_Handle = nullptr;
		DWORD m_Id = 0;
		std::vector<std::pair<DWORD, HANDLE>> m_Threads; // in start order: the first is the main thread
	};
} // namespace C4DAP::Target
