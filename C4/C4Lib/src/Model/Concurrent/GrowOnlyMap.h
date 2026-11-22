#pragma once
#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace C4Compiler
{
	/// A hash map that only grows, read without a lock while it is added to: what lowering on several threads reads of
	/// the model while one of them makes something new (a template instance, a closure). An entry never moves or goes
	/// away, so a pointer or an iterator to it stays good for the map's life; adding takes the map's own lock, finding
	/// takes none and writes nothing shared, so readers do not slow each other. A finder sees a key from the moment
	/// the call that added it returns. It answers to std::unordered_map's names for what it shares with it (find,
	/// emplace, operator[], count, iteration in the order added); nothing is ever erased.
	template <typename K, typename V, typename Hash = std::hash<K>>
	class GrowOnlyMap
	{
	public:
		struct Entry
		{
			Entry(const K& key, V value) : first(key), second(std::move(value)) {}
			const K first;
			V second;
			std::atomic<Entry*> nextAdded{ nullptr }; // the map's own: the entry added after this one
		};

		class iterator
		{
		public:
			explicit iterator(Entry* at = nullptr) : m_At(at) {}
			Entry& operator*() const { return *m_At; }
			Entry* operator->() const { return m_At; }
			iterator& operator++() { m_At = m_At->nextAdded.load(std::memory_order_acquire); return *this; }
			bool operator==(const iterator& other) const { return m_At == other.m_At; }
			bool operator!=(const iterator& other) const { return m_At != other.m_At; }
		private:
			Entry* m_At;
		};

		GrowOnlyMap() : m_State(std::make_unique<State>()) {}

		iterator find(const K& key) const
		{
			const Table* table = m_State->table.load(std::memory_order_acquire);
			for (const Link* link = table->Bucket(Hash()(key)).load(std::memory_order_acquire); link; link = link->next)
				if (link->entry->first == key) return iterator(link->entry);
			return end();
		}

		size_t count(const K& key) const { return find(key) != end() ? 1 : 0; }

		/// The entry of `key`, added with `value` when it has none (true), as std::unordered_map::emplace answers.
		std::pair<iterator, bool> emplace(const K& key, V value)
		{
			State& state = *m_State;
			const std::lock_guard<std::mutex> adding(state.adding);
			if (iterator found = find(key); found != end()) return { found, false };
			Entry* entry = &state.entries.emplace_back(key, std::move(value));
			if (state.entries.size() > state.table.load(std::memory_order_relaxed)->Capacity()) Grow(state);
			else state.table.load(std::memory_order_relaxed)->Publish(entry, Hash()(key));
			if (state.last) state.last->nextAdded.store(entry, std::memory_order_release);
			else state.first.store(entry, std::memory_order_release);
			state.last = entry;
			state.count.store(state.entries.size(), std::memory_order_release);
			return { iterator(entry), true };
		}

		V& operator[](const K& key)
		{
			if (iterator found = find(key); found != end()) return found->second;
			return emplace(key, V()).first->second;
		}

		size_t size() const { return m_State->count.load(std::memory_order_acquire); }
		bool empty() const { return size() == 0; }

		/// The entries in the order added; one added during a walk may or may not be met.
		iterator begin() const { return iterator(m_State->first.load(std::memory_order_acquire)); }
		iterator end() const { return iterator(nullptr); }

	private:
		// A link of a bucket's chain: never changed once its bucket points at it.
		struct Link
		{
			Entry* entry = nullptr;
			const Link* next = nullptr;
		};

		// One size of the table. A grown map has a new one; the old ones stay for the finders still walking them.
		struct Table
		{
			explicit Table(size_t bucketCount) : buckets(new std::atomic<const Link*>[bucketCount]), mask(bucketCount - 1)
			{
				for (size_t i = 0; i < bucketCount; ++i) buckets[i].store(nullptr, std::memory_order_relaxed);
				links.reserve(bucketCount);
			}

			size_t Capacity() const { return mask + 1; }
			std::atomic<const Link*>& Bucket(size_t hash) const { return buckets[Mixed(hash) & mask]; }
			// Pointer keys hash to themselves, alike in their low bits: fold the high ones in.
			static size_t Mixed(size_t hash) { return hash ^ (hash >> 17) ^ (hash >> 7); }

			void Publish(Entry* entry, size_t hash)
			{
				std::atomic<const Link*>& bucket = Bucket(hash);
				links.push_back(Link{ entry, bucket.load(std::memory_order_relaxed) }); // reserved: links never move
				bucket.store(&links.back(), std::memory_order_release);
			}

			std::unique_ptr<std::atomic<const Link*>[]> buckets;
			size_t mask;
			std::vector<Link> links;
		};

		struct State
		{
			State()
			{
				tables.push_back(std::make_unique<Table>(c_FirstBuckets));
				table.store(tables.back().get(), std::memory_order_release);
			}

			std::mutex adding;
			std::deque<Entry> entries;                    // stable: a deque never moves what it holds
			std::vector<std::unique_ptr<Table>> tables;   // every size the table had
			std::atomic<Table*> table{ nullptr };
			std::atomic<Entry*> first{ nullptr };
			Entry* last = nullptr;
			std::atomic<size_t> count{ 0 };
		};

		static constexpr size_t c_FirstBuckets = 16;

		// Twice the buckets, every entry linked anew, then shown to finders at once.
		static void Grow(State& state)
		{
			auto grown = std::make_unique<Table>(state.table.load(std::memory_order_relaxed)->Capacity() * 2);
			for (Entry& entry : state.entries) grown->Publish(&entry, Hash()(entry.first));
			state.table.store(grown.get(), std::memory_order_release);
			state.tables.push_back(std::move(grown));
		}

		std::unique_ptr<State> m_State; // behind a pointer, so the map moves with what owns it
	};
} // namespace C4Compiler
