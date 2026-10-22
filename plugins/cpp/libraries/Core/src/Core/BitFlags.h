#pragma once
#include "Core.h"

namespace Core
{
	class BitFlags
	{
	public:

		BitFlags(uint32 flags = 0u) { SetFlag(flags); }

		void SetFlag(uint32 flag) { m_Flags |= flag; }
		void ClearFlag(uint32 flag) { m_Flags &= ~flag; }
		void SetFlag(uint32 flag, bool val) { if (val) SetFlag(flag); else ClearFlag(flag); }
		bool TestFlag(uint32 flag) const { return (m_Flags & flag) != 0; }

	private:
		uint32 m_Flags = 0;
	};
}