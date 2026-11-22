#pragma once
#include "Core/Core.h"
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace C4Compiler
{
	/// What a request made that the other lowerings may find only once the request has ended (a table's new entries).
	class IShownWhenMade
	{
	public:
		IShownWhenMade() = default;
		IShownWhenMade(IShownWhenMade&&) = default;
		IShownWhenMade& operator=(IShownWhenMade&&) = default;
		virtual ~IShownWhenMade() = default;
		/// The request that made it ended: from now on every lowering finds it.
		virtual void Show() = 0;
	};

	/// How the model changes while lowerings read it on several threads (docs/compiler/02, "Changes while bodies lower").
	/// A change is a request, made once under the model's one lock by the first lowering that asks for it, and answered
	/// from its inputs alone: a template instance from its template and arguments, a closure from the code that writes it.
	/// What a request makes - its types, scopes and table entries - other lowerings find without a lock from when the
	/// outermost request of its thread ends; the thread that makes it finds it at once, so an instance may name itself.
	/// While lowerings share the model (Sharing), changing a shared table outside a request, or a type or scope other
	/// lowerings may already read, is a race: it is kept as a violation, which the lowering driver reports.
	class ModelMaking
	{
	public:
		/// One change of the model, of kind `what` (counted by kind; none for a list the change joins, which is part of
		/// another): the lock held while it lives. Requests nest on the thread that makes.
		class Request
		{
		public:
			Request(const ModelMaking& making, const char* what);
			~Request();
			Request(const Request&) = delete;
			Request& operator=(const Request&) = delete;

		private:
			const ModelMaking& m_Making;
		};

		/// While alive, lowerings may read and change the model on several threads.
		class Sharing
		{
		public:
			explicit Sharing(const ModelMaking& making);
			~Sharing();
			Sharing(const Sharing&) = delete;
			Sharing& operator=(const Sharing&) = delete;

		private:
			const ModelMaking& m_Making;
		};

		/// Whether lowerings share the model now (several threads may read it).
		bool Shared() const { return m_Sharing.load(std::memory_order_acquire) != 0; }
		/// Whether the calling thread is inside a request: it alone changes the model now.
		bool Making() const { return m_Maker.load(std::memory_order_relaxed) == std::this_thread::get_id(); }

		/// Before a change of a table every lowering reads: it must be made inside a request while the model is shared.
		void AssertMaking(const char* what) const
		{
			if (Shared() && !Making()) Violate(what);
		}
		/// Before a change of a type or scope: one other lowerings may read (`shown`) must not change while they share it.
		void AssertUnshown(const std::atomic<bool>& shown, const char* what) const
		{
			if (shown.load(std::memory_order_relaxed) && Shared()) Violate(what);
		}

		/// Inside a request: `made` is shown to the other lowerings when the outermost request ends. Outside one, at once.
		void ShowWhenMade(IShownWhenMade& made) const;
		/// Inside a request: a type or scope it made, which other lowerings read from when the outermost request ends;
		/// from then on it must not change. Outside one it is shown when the next sharing begins (ShowMadeSoFar).
		void Made(std::atomic<bool>& shown) const;

		/// The changes made outside a request, or to what other lowerings read, while the model was shared, each once,
		/// in the order found: empty when there was none. Taken: the next call has only the later ones.
		std::vector<std::string> TakeViolations() const;
		/// Requests asked for, by kind (-ftime).
		std::map<std::string, uint64> Requests() const;

	private:
		void Violate(const char* what) const;
		void Begin(const char* what) const;
		void End() const;

		mutable std::recursive_mutex m_Lock;              // held by the thread that makes
		mutable std::atomic<std::thread::id> m_Maker{};   // that thread, while it holds the lock
		mutable uint32 m_Depth = 0;                       // its requests inside one another
		mutable std::vector<IShownWhenMade*> m_ToShow;     // what its outermost request shows when it ends
		mutable std::vector<std::atomic<bool>*> m_MadeShown; // the types and scopes it made
		mutable std::map<std::string, uint64> m_Requests;  // under m_Lock
		mutable std::atomic<uint32> m_Sharing{ 0 };
		mutable std::mutex m_ViolationsLock;
		mutable std::vector<std::string> m_Violations;
	};
} // namespace C4Compiler
