#include "Target/Patches.h"

namespace C4DAP::Target
{
	namespace
	{
		constexpr uint8_t kInt3 = 0xCC;
	}

	bool Patches::Add(const Process& process, uint64_t address)
	{
		auto found = m_Patches.find(address);
		if (found != m_Patches.end())
		{
			++found->second.users;
			return true;
		}
		Patch patch;
		if (!process.Read(address, patch.original) || !process.Write(address, &kInt3, 1)) return false;
		patch.users = 1;
		m_Patches.emplace(address, patch);
		return true;
	}

	void Patches::Release(const Process& process, uint64_t address)
	{
		auto found = m_Patches.find(address);
		if (found == m_Patches.end() || --found->second.users > 0) return;
		if (!found->second.lifted) process.Write(address, &found->second.original, 1);
		m_Patches.erase(found);
	}

	void Patches::Lift(const Process& process, uint64_t address)
	{
		auto found = m_Patches.find(address);
		if (found == m_Patches.end() || found->second.lifted) return;
		process.Write(address, &found->second.original, 1);
		found->second.lifted = true;
	}

	void Patches::Restore(const Process& process, uint64_t address)
	{
		auto found = m_Patches.find(address);
		if (found == m_Patches.end() || !found->second.lifted) return;
		process.Write(address, &kInt3, 1);
		found->second.lifted = false;
	}
} // namespace C4DAP::Target
