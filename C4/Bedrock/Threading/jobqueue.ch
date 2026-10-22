// The job system's queues (JobSystem, jobs.ch): bounded lock-free multi-producer multi-consumer queues of jobs
// (D. Vyukov's). Each cell carries a sequence number that says whose turn it is, so a producer and a consumer
// each own the cell they claimed with one compare-exchange, and a job is stored by value. The cells are scanned
// by the collector; a taken job's cell is cleared, so the queue keeps no closure alive.

include "waitgroup.ch"

namespace Threading
{
	struct Job
	{
		public func<void()> run;
		public WaitGroup* group;   // told Done when the job ends, or null
		public const char* name;   // what a job observer calls it, or null
	}

	struct JobCell
	{
		public int64 sequence;
		public Job job;
	}

	const int32 kCacheLine = 64;

	struct JobQueue
	{
		[operator("delete")] public void Delete();   // frees the cells; never copied
		[operator("native_bytes")] public int64 NativeBytes() const { return cells ? (mask + 1) * (int64)sizeof(JobCell) : 0; }

		// `capacity` is rounded up to a power of two.
		public void Reserve(int64 capacity);
		// False when the queue is full.
		public bool Push(const Job& job);
		// False when the queue is empty.
		public bool Pop(Job& out);
		public bool IsEmpty() const;

		private JobCell* cells;
		private int64 mask;
		private int64[7] apart;     // the producers' and the consumers' positions live on their own cache lines
		private int64 pushAt;
		private int64[7] apartToo;
		private int64 popAt;
		private int64[7] apartLast;
	}
}
