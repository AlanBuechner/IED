#pragma once
#include <xhash>
#include "Core.h"
#include "Platform/Windows/Win.h"

namespace Core
{
	class UUID
	{
	public:
		UUID();
		UUID(uint64 uuid);
		UUID(const UUID&) = default;

		operator uint64() const { return m_UUID; }
#if defined(PLATFORM_WINDOWS)
		operator LUID() const { return *(LUID*)(void*)&m_UUID; }
#endif

		static UUID Create() { return UUID(); }
		static UUID Create(uint64 uuid) { return UUID(uuid); }

	private:
		uint64 m_UUID;
	};
}


namespace std
{
	template<>
	struct hash<Core::UUID>
	{
		std::size_t operator()(const Core::UUID& uuid) const
		{
			return hash<uint64>()((uint64)uuid);
		}
	};
}
