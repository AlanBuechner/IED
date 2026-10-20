#include "TokenArena.h"

namespace Scripting
{

	TokenArena::TokenArena(uint32 capacity)
		: m_Capacity(capacity)
	{
		if (capacity > 0)
		{
			// Allocate as raw bytes, then placement-new each Token slot.
			m_Buffer = static_cast<Token*>(::operator new(sizeof(Token) * capacity));
			for (uint32 i = 0; i < capacity; i++)
			{
				new (m_Buffer + i) Token();
			}
			m_WritePtr = m_Buffer;
		}
	}

	TokenArena::~TokenArena()
	{
		Release();
	}

	TokenArena::TokenArena(TokenArena&& other) noexcept
		: m_Buffer(other.m_Buffer)
		, m_WritePtr(other.m_WritePtr)
		, m_Count(other.m_Count)
		, m_Capacity(other.m_Capacity)
	{
		other.m_Buffer = nullptr;
		other.m_WritePtr = nullptr;
		other.m_Count = 0;
		other.m_Capacity = 0;
	}

	TokenArena& TokenArena::operator=(TokenArena&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			m_Buffer = other.m_Buffer;
			m_WritePtr = other.m_WritePtr;
			m_Count = other.m_Count;
			m_Capacity = other.m_Capacity;

			other.m_Buffer = nullptr;
			other.m_WritePtr = nullptr;
			other.m_Count = 0;
			other.m_Capacity = 0;
		}
		return *this;
	}

	Token* TokenArena::Alloc()
	{
		if (m_WritePtr >= m_Buffer + m_Capacity)
		{
			// Arena is full - grow the buffer.
			Grow(m_Capacity == 0 ? 64 : m_Capacity * 2);
		}

		Token* slot = m_WritePtr;
		++m_WritePtr;
		++m_Count;
		return slot;
	}

	Token& TokenArena::At(uint32 index) const
	{
		CORE_ASSERT(index < m_Count, "TokenArena::At index out of range", "");
		return m_Buffer[index];
	}

	void TokenArena::DiscardFromByteOffset(size_t byteOffset)
	{
		if (!m_Buffer || byteOffset == 0)
		{
			// Nothing to discard or already at start.
			return;
		}

		// Convert byte offset to token index.
		size_t slotSize = sizeof(Token);
		uint32 keepCount = static_cast<uint32>(byteOffset / slotSize);

		if (keepCount >= m_Count)
		{
			// Offset is beyond or at the end - nothing new to discard.
			return;
		}

		// Destroy tokens being discarded.
		for (uint32 i = keepCount; i < m_Count; i++)
		{
			m_Buffer[i].~Token();
		}

		// Reset the write pointer past the kept tokens.
		m_WritePtr = m_Buffer + keepCount;
		m_Count = keepCount;
	}

	void TokenArena::DiscardFromIndex(uint32 index)
	{
		if (!m_Buffer)
			return;

		if (index >= m_Count)
		{
			Clear();
			return;
		}

		// Destroy tokens being discarded.
		for (uint32 i = index; i < m_Count; i++)
		{
			m_Buffer[i].~Token();
		}

		m_WritePtr = m_Buffer + index;
		m_Count = index;
	}

	void TokenArena::CompactFromIndex(uint32 index)
	{
		if (!m_Buffer || index == 0)
			return;

		if (index >= m_Count)
		{
			// All tokens are before index - discard everything.
			Clear();
			return;
		}

		uint32 keepCount = m_Count - index;

		// Move remaining tokens down to start of buffer using proper move semantics.
		// Token contains Core::String which is not trivially copyable, so memmove would corrupt it.
		for (uint32 i = 0; i < keepCount; i++)
		{
			new (m_Buffer + i) Token(std::move(m_Buffer[index + i]));
		}

		// Destroy the now-duplicate tail tokens.
		for (uint32 i = keepCount; i < m_Count; i++)
		{
			m_Buffer[i].~Token();
		}

		m_WritePtr = m_Buffer + keepCount;
		m_Count = keepCount;
	}

	void TokenArena::Clear()
	{
		m_WritePtr = m_Buffer;
		m_Count = 0;
	}

	void TokenArena::Release()
	{
		if (m_Buffer)
		{
			// Destroy all placed Tokens.
			for (uint32 i = 0; i < m_Capacity; i++)
			{
				m_Buffer[i].~Token();
			}
			::operator delete(m_Buffer);
			m_Buffer = nullptr;
		}
		m_WritePtr = nullptr;
		m_Count = 0;
		m_Capacity = 0;
	}

	void TokenArena::Grow(uint32 newCapacity)
	{
		Token* oldBuffer = m_Buffer;
		uint32 oldCapacity = m_Capacity;

		// Allocate new buffer and placement-new all slots.
		m_Buffer = static_cast<Token*>(::operator new(sizeof(Token) * newCapacity));
		for (uint32 i = 0; i < newCapacity; i++)
		{
			new (m_Buffer + i) Token();
		}

		// Move existing tokens into the new buffer using proper move semantics.
		if (oldBuffer && m_Count > 0)
		{
			for (uint32 i = 0; i < m_Count; i++)
			{
				new (m_Buffer + i) Token(std::move(oldBuffer[i]));
			}
			// Destroy old placed Tokens and free old buffer.
			for (uint32 i = 0; i < oldCapacity; i++)
			{
				oldBuffer[i].~Token();
			}
			::operator delete(oldBuffer);
		}

		m_WritePtr = m_Buffer + m_Count;
		m_Capacity = newCapacity;
	}

} // namespace Scripting
