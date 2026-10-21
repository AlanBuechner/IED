// Pool (D-78): objects of one size kept together in one block - an engine's components, a particle buffer - that the
// collector still tracks one by one. Take() gives a free slot, zeroed; Give() hands one back. A collection ends the
// objects nothing reaches and frees their slots (Take collects once before it says the pool is full), and `released`
// hears of each; with `keepAll` every taken object stays until it is given back, and `keep` may keep any one a
// collection would end. `keep` runs during the collection, while every other thread is stopped: it must not lock or
// Take. A pool owns its memory and gives it back on delete, so it cannot be copied (move it with take()).
//     GC::Pool particles = GC::Pool(sizeof(Particle), 4096);
//     Particle* p = (Particle*)particles.Take();

include "gc.ch"
include "Container/array.ch"
include "Threading/mutex.ch"

namespace GC
{
	struct PoolOptions
	{
		public bool noscan;                   // the objects hold no managed pointers: marking skips them
		public bool keepAll;                  // a taken object stays alive until Give
		public func<bool(void*)> keep;        // optional: keep this object a collection would end?
		public func<void(void*)> released;    // optional: a collection ended this object; its slot is free again
	}

	// What a pool shares with the collector's callbacks; on the heap, so the pool can move.
	class PoolState
	{
		public void* handle;                  // the runtime's pool
		public PoolOptions options;
		public Threading::Mutex mutex;
		public Core::Array<int64> free;       // slots given back or ended
		public int64 fresh;                   // slots never taken lie from here on
		public int64 count;
		public int64 slotBytes;
	}

	class Pool
	{
		[operator("create")] public void Init() {}
		// `count` slots of `slotBytes` (rounded up to 16, at most 1 MiB); closed when the sizes are out of range.
		[operator("create")] public void Init(int64 slotBytes, int64 count) { Open(slotBytes, count, PoolOptions()); }
		[operator("create")] public void Init(int64 slotBytes, int64 count, PoolOptions options) { Open(slotBytes, count, options); }
		[operator("delete")] public void Delete() { Close(); }

		public bool IsOpen() const { return state != nullptr; }
		// A zeroed free slot, now taken; nullptr when every slot holds an object something reaches.
		public void* Take();
		// Frees a taken slot now (`released` is not called).
		public void Give(void* object);
		// Whether `object` is a taken slot of this pool.
		public bool Holds(const void* object) const;
		public void* At(int64 index) const;
		// The slot `object` starts, or -1.
		public int64 IndexOf(const void* object) const;
		public int64 SlotBytes() const { return state ? state->slotBytes : 0; }
		public int64 Count() const { return state ? state->count : 0; }
		// Gives the memory back; the pool's objects end with it. Idempotent.
		public void Close();

	private:
		private void Open(int64 slotBytes, int64 count, PoolOptions options);
		private int64 FreeSlot();   // -1 when none

		private PoolState* state;
	}
}
