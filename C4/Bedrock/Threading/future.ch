// Promise<T> and Future<T>: one value handed from the thread that makes it to the threads that wait for it
// (std::promise / std::shared_future). A Promise makes the shared state; Set stores the value once and wakes the
// waiters. Copies of either share the state, which the collector keeps while any copy refers to it. A zero Future
// has no state and is never ready.
//     Threading::Promise<int32> promise;
//     Threading::Future<int32> answer = promise.GetFuture();
//     ... promise.Set(42); ... answer.Get();

namespace Threading
{
	const int32 kFutureEmpty = 0;
	const int32 kFutureSetting = 1;
	const int32 kFutureReady = 2;

	template<T> class FutureState
	{
		public int32 stage;   // kFutureEmpty, kFutureSetting, kFutureReady
		public T value;
	}

	template<T> struct Future
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(FutureState<T>* shared) { state = shared; }

		public bool IsValid() const { return state != nullptr; }
		public bool IsReady() const { return state != nullptr && atomic_load(&state->stage, MemoryOrder::Acquire) == kFutureReady; }
		// Waits until the value is set. A zero Future waits forever.
		public void Wait() const { WaitFor(-1); }
		// The same for at most `nanoseconds`; false when the value is not set yet.
		public bool WaitFor(int64 nanoseconds) const {
			if (state == nullptr) {
				int32 never = 0;
				wait_on_address((void*)&never, (void*)&never, 4, nanoseconds);
				return false;
			}
			int32 seen = atomic_load(&state->stage, MemoryOrder::Acquire);
			while (seen != kFutureReady) {
				if (!wait_on_address((void*)&state->stage, (void*)&seen, 4, nanoseconds)) return IsReady();
				seen = atomic_load(&state->stage, MemoryOrder::Acquire);
			}
			return true;
		}
		// The value, once set (waits until it is).
		public const T& Get() const { Wait(); return state->value; }

		private FutureState<T>* state;
	}

	template<T> struct Promise
	{
		[operator("create")] public void Init() { state = new FutureState<T>; }

		public Future<T> GetFuture() const { return Future<T>(state); }
		// Stores the value and wakes every waiter; false when a value was set already (this one is dropped).
		public bool Set(const T& value) {
			int32 empty = kFutureEmpty;
			if (!atomic_compare_exchange(&state->stage, &empty, kFutureSetting, MemoryOrder::Acquire, MemoryOrder::Relaxed)) return false;
			state->value = value;
			atomic_store(&state->stage, kFutureReady, MemoryOrder::Release);
			wake_all((void*)&state->stage);
			return true;
		}

		private FutureState<T>* state;
	}
}
