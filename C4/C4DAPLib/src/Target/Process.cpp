#include "Target/Process.h"
#include <algorithm>

namespace C4DAP::Target
{
	void Process::Started(const CREATE_PROCESS_DEBUG_INFO& info, DWORD processId, DWORD threadId)
	{
		m_Handle = info.hProcess;
		m_Id = processId;
		m_Threads.clear();
		m_Threads.emplace_back(threadId, info.hThread);
	}

	void Process::ThreadStarted(DWORD threadId, HANDLE thread)
	{
		m_Threads.emplace_back(threadId, thread);
	}

	void Process::ThreadEnded(DWORD threadId)
	{
		std::erase_if(m_Threads, [threadId](const auto& entry) { return entry.first == threadId; });
	}

	void Process::Ended()
	{
		m_Handle = nullptr;
		m_Threads.clear();
	}

	HANDLE Process::Thread(DWORD threadId) const
	{
		for (const auto& [id, handle] : m_Threads)
			if (id == threadId) return handle;
		return nullptr;
	}

	std::vector<DWORD> Process::Threads() const
	{
		std::vector<DWORD> ids;
		for (const auto& entry : m_Threads) ids.push_back(entry.first);
		return ids;
	}

	bool Process::Read(uint64_t address, void* into, size_t size) const
	{
		SIZE_T got = 0;
		return m_Handle && ReadProcessMemory(m_Handle, reinterpret_cast<LPCVOID>(address), into, size, &got) && got == size;
	}

	bool Process::Write(uint64_t address, const void* from, size_t size) const
	{
		SIZE_T wrote = 0;
		if (!m_Handle || !WriteProcessMemory(m_Handle, reinterpret_cast<LPVOID>(address), from, size, &wrote) || wrote != size) return false;
		FlushInstructionCache(m_Handle, reinterpret_cast<LPCVOID>(address), size);
		return true;
	}

	std::string Process::ReadText(uint64_t address, size_t limit, bool& complete) const
	{
		constexpr size_t kChunk = 64;
		std::string text;
		complete = false;
		while (text.size() < limit)
		{
			char chunk[kChunk];
			const size_t want = std::min(kChunk, limit - text.size());
			size_t got = want;
			// A string can end just before an unreadable page: fall back to reading byte by byte.
			if (!Read(address + text.size(), chunk, want))
				for (got = 0; got < want && Read(address + text.size() + got, chunk[got]); ++got) {}
			for (size_t i = 0; i < got; ++i)
			{
				if (chunk[i] == '\0') { complete = true; return text; }
				text.push_back(chunk[i]);
			}
			if (got < want) return text;
		}
		return text;
	}

	bool Process::Registers(DWORD threadId, CONTEXT& context) const
	{
		const HANDLE thread = Thread(threadId);
		context = {};
		context.ContextFlags = CONTEXT_FULL;
		return thread && GetThreadContext(thread, &context);
	}

	bool Process::SetRegisters(DWORD threadId, const CONTEXT& context) const
	{
		const HANDLE thread = Thread(threadId);
		return thread && SetThreadContext(thread, &context);
	}
} // namespace C4DAP::Target
