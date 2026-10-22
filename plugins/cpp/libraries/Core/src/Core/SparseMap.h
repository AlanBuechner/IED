#pragma once

#include "Types.h"
#include "Vector.h"
#include <concepts>

namespace Core
{
	/// Sparse map keyed by integral type (e.g. entity IDs). Provides O(1) insert/lookup/delete
	/// with compact contiguous value storage via a dense/sparse pair of arrays.
	/// Keys are used as direct indices into the sparse table, which grows as needed.
	template<std::integral KeyType, typename ValueType>
	class SparseMap
	{
	public:
		struct Entry
		{
			KeyType key;
			ValueType value;
		};

	public:
		SparseMap() = default;
		~SparseMap() = default;

		/// Insert or update a value for the given key. Returns a reference to the stored value.
		ValueType& Insert(KeyType key, const ValueType& value)
		{
			if (Contains(key))
			{
				operator[](key) = value;
				return operator[](key);
			}

			uint32 index = static_cast<uint32>(m_Dense.Count());
			m_Dense.Push({ key, value });
			EnsureSparseCapacity(key);
			m_Sparse[key] = static_cast<KeyType>(index);

			return m_Dense.Back().value;
		}

		/// Insert a default-constructed value for the given key. Returns a reference to it.
		ValueType& Insert(KeyType key)
		{
			if (Contains(key))
				return operator[](key);

			uint32 index = static_cast<uint32>(m_Dense.Count());
			m_Dense.Push({ key, {} });
			EnsureSparseCapacity(key);
			m_Sparse[key] = static_cast<KeyType>(index);

			return m_Dense.Back().value;
		}

		/// Push a new entry, reusing the first free slot in the sparse array if available.
		/// Returns the assigned key.
		KeyType Push(const ValueType& value)
		{
			for (uint32 i = 0; i < m_Sparse.Count(); ++i)
			{
				if (m_Sparse[i] == kInvalidIndex)
				{
					KeyType key = static_cast<KeyType>(i);
					m_Dense.Push({ key, value });
					m_Sparse[key] = static_cast<KeyType>(m_Dense.Count() - 1);
					return key;
				}
			}

			// No free slot - allocate a new one beyond the current sparse capacity
			KeyType key = static_cast<KeyType>(m_Sparse.Count());
			m_Dense.Push({ key, value });
			m_Sparse.Push(static_cast<KeyType>(m_Dense.Count() - 1));
			return key;
		}

		/// Push a default-constructed entry by reusing the first free slot. Returns the assigned key.
		KeyType Push()
		{
			for (uint32 i = 0; i < m_Sparse.Count(); ++i)
			{
				if (m_Sparse[i] == kInvalidIndex)
				{
					KeyType key = static_cast<KeyType>(i);
					m_Dense.Push({ key, {} });
					m_Sparse[key] = static_cast<KeyType>(m_Dense.Count() - 1);
					return key;
				}
			}

			KeyType key = static_cast<KeyType>(m_Sparse.Count());
			m_Dense.Push({ key, {} });
			m_Sparse.Push(static_cast<KeyType>(m_Dense.Count() - 1));
			return key;
		}

		/// Get a mutable reference to the value for the given key. Behavior is undefined if the key does not exist.
		ValueType& operator[](KeyType key)
		{
			uint32 index = static_cast<uint32>(m_Sparse[key]);
			return m_Dense[index].value;
		}

		const ValueType& operator[](KeyType key) const
		{
			uint32 index = static_cast<uint32>(m_Sparse[key]);
			return m_Dense[index].value;
		}

		/// Remove the entry for the given key. Returns true if the key existed.
		bool Remove(KeyType key)
		{
			if (!Contains(key))
				return false;

			uint32 removedIndex = static_cast<uint32>(m_Sparse[key]);
			uint32 lastIndex = static_cast<uint32>(m_Dense.Count() - 1);

			if (removedIndex != lastIndex)
			{
				// Move the last entry into the removed slot to keep dense array compact
				m_Dense[removedIndex] = std::move(m_Dense[lastIndex]);
				m_Sparse[m_Dense[removedIndex].key] = static_cast<KeyType>(removedIndex);
			}

			m_Sparse[key] = kInvalidIndex;
			m_Dense.Pop();

			return true;
		}

		/// Check whether the key exists in the map.
		bool Contains(KeyType key) const
		{
			return static_cast<uint32>(key) < m_Sparse.Count() && m_Sparse[key] != kInvalidIndex;
		}

		/// Number of live entries.
		uint32 Count() const { return static_cast<uint32>(m_Dense.Count()); }

		const Core::Vector<Entry>& Dense() const { return m_Dense; }

		bool Empty() const { return m_Dense.Empty(); }

		/// Reserve capacity for the dense array (value storage).
		void Reserve(uint32 capacity) { m_Dense.Reserve(capacity); }

		/// Clear all entries. Keeps allocated memory.
		void Clear()
		{
			m_Dense.Clear();
			for (uint32 i = 0; i < m_Sparse.Count(); ++i)
				m_Sparse[i] = kInvalidIndex;
		}

	private:
		static constexpr KeyType kInvalidIndex = static_cast<KeyType>(-1);

		/// Grows the sparse array if the given key is out of bounds.
		void EnsureSparseCapacity(KeyType key)
		{
			uint32 needed = static_cast<uint32>(key) + 1;
			if (needed > m_Sparse.Count())
			{
				m_Sparse.Resize(needed, kInvalidIndex);
			}
		}

		Core::Vector<Entry> m_Dense;       // Contiguous storage of live entries
		Core::Vector<KeyType> m_Sparse;   // Direct index-by-key array: key -> dense slot or -1
	};
}
