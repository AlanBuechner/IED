#pragma once
#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>

namespace Scripting
{
	/// A list that only grows at its end, walked without a lock while it is added to (see GrowOnlyMap): an element
	/// never moves or goes away, and a walk sees the elements added before it reached the end. It answers to
	/// std::deque's names for what it shares with it (push_back, front, size, iteration).
	template <typename T>
	class GrowOnlyList
	{
		struct Node
		{
			explicit Node(T v) : value(std::move(v)) {}
			T value;
			std::atomic<Node*> next{ nullptr };
		};

	public:
		GrowOnlyList() : m_State(std::make_unique<State>()) {}

		T& push_back(T value)
		{
			State& state = *m_State;
			const std::lock_guard<std::mutex> adding(state.adding);
			Node* node = &state.nodes.emplace_back(std::move(value));
			if (state.last) state.last->next.store(node, std::memory_order_release);
			else state.first.store(node, std::memory_order_release);
			state.last = node;
			state.count.store(state.nodes.size(), std::memory_order_release);
			return node->value;
		}

		size_t size() const { return m_State->count.load(std::memory_order_acquire); }
		bool empty() const { return size() == 0; }
		/// The first element; the list is not empty.
		T& front() const { return m_State->first.load(std::memory_order_acquire)->value; }

		class Walk
		{
		public:
			explicit Walk(Node* at) : m_At(at) {}
			T& operator*() const { return m_At->value; }
			T* operator->() const { return &m_At->value; }
			Walk& operator++() { m_At = m_At->next.load(std::memory_order_acquire); return *this; }
			bool operator!=(const Walk& other) const { return m_At != other.m_At; }
		private:
			Node* m_At;
		};

		Walk begin() const { return Walk(m_State->first.load(std::memory_order_acquire)); }
		Walk end() const { return Walk(nullptr); }

	private:
		struct State
		{
			std::mutex adding;
			std::deque<Node> nodes; // stable: a deque never moves what it holds
			std::atomic<Node*> first{ nullptr };
			Node* last = nullptr;
			std::atomic<size_t> count{ 0 };
		};

		std::unique_ptr<State> m_State; // behind a pointer, so the list moves with what owns it
	};
} // namespace Scripting
