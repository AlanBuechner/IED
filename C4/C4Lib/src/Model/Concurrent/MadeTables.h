#pragma once
#include "Model/Concurrent/GrowOnlyList.h"
#include "Model/Concurrent/GrowOnlyMap.h"
#include "Model/ModelMaking.h"
#include <deque>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Scripting
{
	/// An answer table lowering's requests fill, keyed by what the answer is made from (a template and its arguments):
	/// every lowering finds an answer without a lock once the request that made it has ended, and the thread making it
	/// finds it at once (an instance names itself while its body is built). A failed answer is forgotten before it is
	/// shown. Outside a request (the model being built) an answer is shown when added.
	template <typename K, typename V, typename Hash = std::hash<K>>
	class MadeMap final : public IShownWhenMade
	{
	public:
		MadeMap(const ModelMaking& making, const char* what) : m_Making(&making), m_What(what) {}

		const V* Find(const K& key) const
		{
			if (auto shown = m_Shown.find(key); shown != m_Shown.end()) return &shown->second;
			if (!m_Making->Making()) return nullptr;
			auto pending = m_Pending.find(key);
			return pending != m_Pending.end() ? &pending->second : nullptr;
		}

		void Add(const K& key, V value)
		{
			m_Making->AssertMaking(m_What);
			if (!m_Making->Making())
			{
				m_Shown.emplace(key, std::move(value));
				return;
			}
			m_Pending.insert_or_assign(key, std::move(value));
			m_Making->ShowWhenMade(*this);
		}

		/// A failed answer, before the request that began it ends: a later request may try again.
		void Forget(const K& key) { m_Pending.erase(key); }

		void Show() override
		{
			for (auto& pending : m_Pending) m_Shown.emplace(pending.first, std::move(pending.second));
			m_Pending.clear();
		}

	private:
		const ModelMaking* m_Making;
		const char* m_What;
		GrowOnlyMap<K, V, Hash> m_Shown;
		std::unordered_map<K, V, Hash> m_Pending; // the making thread's alone
	};

	/// Lists of what lowering's requests add to, by key (the type templates of a name): an element is listed for every
	/// lowering once the request that added it has ended, and for the thread adding it at once, after the shown ones.
	/// Elements never move.
	template <typename K, typename T, typename Hash = std::hash<K>>
	class MadeLists final : public IShownWhenMade
	{
	public:
		MadeLists(const ModelMaking& making, const char* what) : m_Making(&making), m_What(what) {}

		/// Each element of `key`'s list, in the order added.
		template <typename Visit>
		void ForEach(const K& key, Visit&& visit) const
		{
			if (auto shown = m_Shown.find(key); shown != m_Shown.end())
				for (const T* element : *shown->second) visit(*element);
			if (!m_Making->Making()) return;
			for (const auto& [pendingKey, element] : m_Pending)
				if (pendingKey == key) visit(*element);
		}

		/// The first element of `key`'s list; null when it has none.
		const T* First(const K& key) const
		{
			const T* first = nullptr;
			ForEach(key, [&](const T& element) { if (!first) first = &element; });
			return first;
		}

		bool Has(const K& key) const { return First(key) != nullptr; }

		const T& Add(const K& key, T element)
		{
			m_Making->AssertMaking(m_What);
			const T* stored = &m_Elements.emplace_back(std::move(element));
			if (!m_Making->Making()) ListOf(key).push_back(stored);
			else
			{
				m_Pending.emplace_back(key, stored);
				m_Making->ShowWhenMade(*this);
			}
			return *stored;
		}

		void Show() override
		{
			for (const auto& [key, element] : m_Pending) ListOf(key).push_back(element);
			m_Pending.clear();
		}

	private:
		GrowOnlyList<const T*>& ListOf(const K& key)
		{
			return *m_Shown.emplace(key, std::make_unique<GrowOnlyList<const T*>>()).first->second;
		}

		const ModelMaking* m_Making;
		const char* m_What;
		std::deque<T> m_Elements; // every element, never moved; added to by one thread at a time
		GrowOnlyMap<K, std::unique_ptr<GrowOnlyList<const T*>>, Hash> m_Shown;
		std::vector<std::pair<K, const T*>> m_Pending; // the making thread's alone
	};
} // namespace Scripting
