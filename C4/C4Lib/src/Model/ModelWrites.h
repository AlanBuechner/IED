#pragma once
#include <atomic>
#include <functional>

namespace Scripting
{
	/// A change asked of a frozen model. Lowering on several threads reads a frozen model; a function whose lowering
	/// would change it (an instance, a record, a type, a scope, a memo) stops with this and is lowered again later,
	/// where the change is made (ParallelLowering).
	struct ModelFrozen
	{
		const char* what; // the kind of change, for -ftime
		/// The change itself, when it can be made apart from the lowering that asked for it (a template instance,
		/// made from its template and arguments alone): made between rounds, after which the lowering finds it.
		std::function<void()> make;
	};

	/// Whether the model may change now. Every place that changes what lowering reads asks first: while the model is
	/// frozen the asking lowering stops with ModelFrozen, so the threads reading the model never see it change.
	class ModelWrites
	{
	public:
		void Freeze(bool frozen) { m_Frozen.store(frozen, std::memory_order_relaxed); }
		bool Frozen() const { return m_Frozen.load(std::memory_order_relaxed); }

		/// Before a change of kind `what`: throws ModelFrozen while the model is frozen.
		void Ask(const char* what) const
		{
			if (Frozen()) throw ModelFrozen{ what, {} };
		}

		/// The same, for a change `make` makes on its own.
		void Ask(const char* what, const std::function<void()>& make) const
		{
			if (Frozen()) throw ModelFrozen{ what, make };
		}

	private:
		std::atomic<bool> m_Frozen{ false };
	};
}
