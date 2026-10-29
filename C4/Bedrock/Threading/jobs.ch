// JobSystem: worker threads that run func<void()> jobs, tuned so a time-critical job never waits for a worker
// behind long work (D-61, docs/library/bedrock.md section 10). High and Normal jobs are frame work: short, due this
// frame. Background jobs may run long (loading, decompression, pathfinding) and run only on background workers,
// which also take frame work when they have none. Wait(group) runs frame jobs on the waiting thread until the
// group is done. Every queue is lock-free; an idle worker spins briefly, then parks.
//     Threading::JobSystem jobs;
//     Threading::WaitGroup group;
//     for (int32 i = 0; i < 64; i++) jobs.Submit([i]() { Simulate(i); }, Threading::JobPriority::High, &group);
//     jobs.Wait(group);

include "jobqueue.ch"
include "thread.ch"
include "Container/array.ch"

namespace Threading
{
	enum class JobPriority { High, Normal, Background }

	struct JobSystemDesc
	{
		public int32 frameWorkers = -1;        // -1: the processors minus one, at least 1
		public int32 backgroundWorkers = -1;   // -1: one; 0: frame workers run background jobs after frame work
	}

	// One frame worker's own queues, by priority: the jobs it submits go there first.
	class FrameQueues
	{
		public JobQueue high;
		public JobQueue normal;
	}

	// Everything the workers share; the JobSystem and every worker refer to it, so the JobSystem may move.
	class JobWorkers
	{
		public Core::Array<FrameQueues*> frame;
		public JobQueue[3] shared;   // by JobPriority: jobs submitted by any thread that is not a frame worker
		public Core::Array<void*> threads;
		public int32 backgroundCount;
		public int32 stopping;
		public int32 frameSignal;    // bumped to wake a parked frame worker
		public int32 frameSleepers;
		public int32 backgroundSignal;
		public int32 backgroundSleepers;

		// The next frame job for frame worker `self` (-1: any other thread), High before Normal: its own queue,
		// the shared one, then the other frame workers'.
		public bool TakeFrameJob(int32 self, Job& out);
		public bool TakeBackgroundJob(Job& out);
		public bool HasFrameWork() const;
		// Spins, then parks until a submit; false once the system stops and nothing is left for this worker.
		public bool WaitForWork(bool background);
		public void WakeFor(JobPriority priority);
		public void WakeEveryone();
	}

	class JobSystem
	{
		[operator("create")] public void Init() { JobSystemDesc defaults; Start(defaults); }
		[operator("create")] public void Init(JobSystemDesc desc) { Start(desc); }
		// Runs the jobs still queued, then joins the workers.
		[operator("delete")] public void Delete();

		public void Submit(func<void()> job) { Submit(job, JobPriority::Normal, nullptr); }
		public void Submit(func<void()> job, JobPriority priority) { Submit(job, priority, nullptr); }
		// `group`, when given, counts the job until it ends: Submit adds it to the group itself, so the caller does not
		// Add for it (an Add of its own as well leaves the group waiting forever).
		public void Submit(func<void()> job, JobPriority priority, WaitGroup* group) { Submit(job, priority, group, nullptr); }
		// `name`, when given, is what a job observer (SetJobObserver) calls the job: a literal, or text that outlives it.
		public void Submit(func<void()> job, JobPriority priority, WaitGroup* group, const char* name);
		// Runs frame jobs on this thread until `group` is done.
		public void Wait(WaitGroup& group);

		public int32 FrameWorkerCount() const { return workers ? (int32)workers->frame.Count() : 0; }
		public int32 BackgroundWorkerCount() const { return workers ? workers->backgroundCount : 0; }

		private void Start(JobSystemDesc desc);
		private bool Enqueue(const Job& job, JobPriority priority);

		private JobWorkers* workers;
	}

	// What a profiler is told of each job: begin(name) on the worker before the job runs, then end(token) with what
	// begin returned once it has. A function left null is not called.
	struct JobObserver
	{
		public funcptr<uint64(const char*)> begin;
		public funcptr<void(uint64)> end;
	}

	// The observer every JobSystem in the process tells of its jobs: a profiler sets it as it starts and sets an empty
	// one as it stops. Process-wide, because the job systems a program makes are not the profiler's to reach.
	void SetJobObserver(JobObserver observer);
}
