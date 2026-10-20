// Atomic<T>: one value of an integer, bool, enum, pointer or float type that threads read and change with
// indivisible operations (std::atomic). Each operation takes a MemoryOrder, sequentially consistent by default;
// a constant order passed through inlines to exactly that operation. A copy is an atomic snapshot of the value.
// The Fetch operations need an integer T. Zero is a zero value.

namespace Threading
{
	template<T> struct Atomic
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(T initial) { value = initial; }
		[operator("copy")] public void CopyFrom(const Atomic<T>& other) { value = other.Load(); }

		public T Load(MemoryOrder order = MemoryOrder::SeqCst) const { return atomic_load(&value, order); }
		public void Store(T desired, MemoryOrder order = MemoryOrder::SeqCst) { atomic_store(&value, desired, order); }
		// The value it held.
		public T Exchange(T desired, MemoryOrder order = MemoryOrder::SeqCst) { return atomic_exchange(&value, desired, order); }

		// When the value is `expected`, makes it `desired` and returns true; otherwise writes the value to `expected`.
		// A failed exchange only loads: it orders as `order` without its release.
		public bool CompareExchange(T& expected, T desired, MemoryOrder order = MemoryOrder::SeqCst) {
			return atomic_compare_exchange(&value, &expected, desired, order, FailureOrder(order));
		}
		// The same, but it may fail although the value was `expected`: for retry loops.
		public bool CompareExchangeWeak(T& expected, T desired, MemoryOrder order = MemoryOrder::SeqCst) {
			return atomic_compare_exchange_weak(&value, &expected, desired, order, FailureOrder(order));
		}

		// Combine the value with `operand`, returning the value it held.
		public T FetchAdd(T operand, MemoryOrder order = MemoryOrder::SeqCst) { return atomic_fetch_add(&value, operand, order); }
		public T FetchSub(T operand, MemoryOrder order = MemoryOrder::SeqCst) { return atomic_fetch_sub(&value, operand, order); }
		public T FetchAnd(T operand, MemoryOrder order = MemoryOrder::SeqCst) { return atomic_fetch_and(&value, operand, order); }
		public T FetchOr(T operand, MemoryOrder order = MemoryOrder::SeqCst) { return atomic_fetch_or(&value, operand, order); }
		public T FetchXor(T operand, MemoryOrder order = MemoryOrder::SeqCst) { return atomic_fetch_xor(&value, operand, order); }

		// Where the value lives, to wait on it (wait_on_address) or wake its waiters.
		public void* Address() const { return (void*)&value; }

		private static MemoryOrder FailureOrder(MemoryOrder order) {
			if (order == MemoryOrder::Release) return MemoryOrder::Relaxed;
			if (order == MemoryOrder::AcqRel) return MemoryOrder::Acquire;
			return order;
		}

		private T value;
	}
}
