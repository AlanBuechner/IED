---
name: profiler
description: Find where a program spends its time and memory with IED's profiler - read captures with the profile_* tools, find lock contention, leave notes on a capture, record one with profile_capture, instrument C4, C/C++, Go or Rust code, and import Chrome traces and pprof profiles.
keywords: [profile, profiler, performance, slow, frame time, hitch, stutter, spike, latency, flame graph, zones, capture, trace, pprof, leak, allocation, gc pause, lock, mutex, contention, deadlock, annotate, note]
---

# Profiler

IED's profiler records **captures**: what an instrumented program did over time - zones (named spans of code) on each
thread, frames, plots, log messages, tasks, locks, allocations, garbage collections. Captures live in the workspace's
`.ied/local/captures/` as `.iedtrace` files. The owner sees them in the Profiler activity; you read them with the
`profile_*` tools, which answer from the same data.

## Reading a capture

Work from the top down; each tool names zones by their source file and line.

1. `profile_list` - the captures, newest first. Most tools default to `capture: "latest"`.
2. `profile_summary` - duration, frame times (mean, median, p95, p99, max), the costliest zones by **self** time (time
   in the zone itself, not its children) and by **total** time, tasks, memory and GC, and any warnings (a warning
   means the program's instrumentation was unbalanced - say so, it can skew numbers).
3. For stutter or a slow frame: `profile_frame` (`index: "worst"` by default) - each thread's work in that frame,
   its costliest zones, collections and GPU work in it. Compare with the median frame from the summary.
4. `profile_top_zones` - rank by `self` to find where the CPU goes, by `total` for which subsystem is expensive, by
   `max` or `p95` for spikes; `group_by: "thread"` or `"task"` splits a zone by where it ran; `from_ms`/`to_ms`
   limit to a range (from `profile_frame` or the summary).
5. `profile_find_zone` - one zone's distribution: whether it is always slow (raise the median) or sometimes slow
   (look at the slowest instances and when they happened).
6. `profile_flame` - the call tree merged by name: where a zone's time goes beneath it.
7. `profile_messages` - the program's own log in time order, filtered by severity or text.
8. `profile_leaks` - allocations still live at the end (or `at_ms`), by call stack, pool or thread; with the pools'
   peaks and GC pauses.
9. `profile_locks` - when threads are busy but frames are slow, or work that should run in parallel does not: each
   lock's **waits** (time a thread was blocked on it), **contended** holds (another thread waited as it was released),
   holds and threads, most waited for first, and the longest waits with the thread and the time. The summary lists the
   three busiest. A lock "still waited for at the end" is a deadlock or a stuck thread. Fix contention by holding the
   lock for less (move work out of it), splitting it, or not sharing the data; a short hold taken very often costs too.
10. After a change: record again and `profile_compare` the two captures (`a` before, `b` after).

Report findings with numbers (median and p95, not only the mean), the zone's source location, and what you would
change; open the source before proposing an edit.

## Notes on a capture

`profile_annotate` leaves a note on a time range (`from_ms`, `to_ms`; a moment without `to_ms`), kept in the capture's
trace file. The owner sees it on the timeline and in the Notes window, marked as the AI's. Mark what you found where it
happens - "Physics waits 4.1 ms for the Assets lock held by Loader" - one note per finding, short, with the number;
not a running commentary. The summary lists every note, the owner's too: read theirs, they say what to look at.
Times in the tools' answers are milliseconds from the capture's start, the same as `from_ms` takes.

## Recording

- `profile_capture` builds and runs the startup project's **Profile** task, records for `seconds` while the program
  streams, then stops it and returns the new capture's summary. The task must start the program with
  `IED_PROFILER=${profiler.connect}` (the Nuke preset's Profile task does). The program must be instrumented.
- The owner can do the same with Debug > Start Profiling (Alt+F2).
- A program started outside IED connects when its environment has `IED_PROFILER=<value>` (Debug > Connect a Program to
  the Profiler shows the value). `IED_PROFILER=file:<path>.iedtrace` writes a trace file instead; open it in IED.

## Instrumenting C4 (Bedrock `Profile::`)

```c4
include "profile.ch"                     // opt-in: bedrock.ch does not include it

void Update(World& world) {
	PROFILE_ZONE("Update");               // a zone to the end of the scope, with this file and line
	{
		Profile::Zone zone(Core::StringView("Physics"));   // a zone named at run time
		zone.Text(Core::StringView("bodies=2048"));
		world.Step();
	}
	Profile::Plot(Core::StringView("Entities"), (double)world.Count());
}

int32 main() {
	Profile::SetThreadName(Core::StringView("Main"));       // call again whenever the thread changes role
	while (running) {
		Update(world);
		Profile::Frame();                 // once per presented frame
	}
}
```

- Workers: `Profile::SetThreadName(...)` as a pooled thread takes a job (the profiler shows renames over time), and
  `Profile::TaskScope task(Core::StringView("Decompress"));` (or `TaskBegin`/`TaskSuspend`/`TaskResume`/`TaskEnd`)
  so the **Tasks** view groups the work by task wherever it ran.
- `Profile::Message(text, Profile::Severity::Warning)` for log lines worth seeing on the timeline.
- `Profile::Alloc(ptr, size, pool)` / `Profile::Free(ptr, pool)` / `Profile::PoolName(pool, name)` for the program's
  own allocators (arenas, pools); the GC's collections and heap size are reported by themselves.
- Locks: `PROFILE_MUTEX(s_queueLock, "Queue");` makes a `Profile::Mutex` (`Lock`, `TryLock`, `Unlock`, or
  `Profile::MutexGuard guard(s_queueLock);` for a scope) whose waits and holds are recorded; a lock of the program's
  own reports through a `Profile::LockSite` with `Profile::LockWait`, `LockObtained` and `LockReleased`.
- `JobSystem` jobs are tasks by themselves, named as they were submitted, on workers named "Job Worker N".
- Hot code: prefer `PROFILE_ZONE` (a static site, no lookup) over a run-time name. Zones cost tens of nanoseconds;
  do not put one around a loop body that runs millions of times - put it around the loop.
- `-D PROFILE=0` (a configuration's `defines` in `nuke.toml`) compiles the `PROFILE_*` macros away for shipping.

## Instrumenting C and C++

Copy `Itp.h` and `Itp.cpp` from `C4/Frost/Projects/Profiling/src/Profiling/` into the project (no other dependency;
Windows links `ws2_32`, POSIX needs `-pthread`). Frost projects already have them: build with `ENABLE_PROFILING`
and use `CREATE_PROFILE_SCOPEI("Name")`, `CREATE_PROFILE_FUNCTIONI()`, `FRAME_MARK` from `Profiling/Performance.h`.

```cpp
#include "Itp.h"

void Update() {
	ITP_ZONE("Update");                       // to the end of the scope, with function, file and line
	itp_plot("Entities", (double)count);
}
int main() {
	itp_thread_name("Main");                  // again whenever a thread takes another role
	while (running) { Update(); ITP_FRAME(); }
}
```

- `itp::Task task("Decompress");` for the Tasks view; `itp_message(ITP_WARNING, text, length)`; `itp_alloc` /
  `itp_free` / `itp_pool_name` (safe inside a replaced `operator new`).
- Locks: `static ITP_LOCKABLE(std::mutex, queue, "Queue");` is an `itp::Lockable<std::mutex>`, usable with
  `std::lock_guard` and `std::unique_lock` (a `std::shared_mutex` records shared holds too); in C, an `itp_lock` with
  `itp_lock_wait`, `itp_lock_obtained` and `itp_lock_released` around the program's own lock.
- C: `ITP_ZONE_BEGIN("Name")` ... `ITP_ZONE_END()`.
- `ITP_ENABLE=0` compiles the `ITP_*` macros away; the functions stay and cost one load without `IED_PROFILER`.

## Instrumenting Go

The module `github.com/AlanBuechner/IED/plugins/go/profiler` (standard library only; in a module outside this
repository, `replace` it with the plugin's folder). The go preset's Profile task runs `go run .` with `IED_PROFILER`.

```go
func main() {
	defer profiler.Stop()                              // Go runs nothing at exit: this sends the last events
	ctx := profiler.WithThread(context.Background(), "Main")
	for running {
		update(ctx)
		profiler.Frame()
	}
}

func update(ctx context.Context) {
	defer profiler.Zone(ctx, "Update").End()          // at this file and line
	profiler.Plot("Entities", float64(len(world)))
}
```

- Each goroutine records on its own track: give a long-lived one (a worker) `profiler.WithThread(ctx, name)` once,
  and pass that context to its zones; a goroutine without one gets "goroutine N" (a microsecond to find it).
- `ctx, task := profiler.Task(ctx, "Request")` ... `task.End()` groups work across goroutines in the Tasks view.
- Locks: `var queue = profiler.NewMutex("Queue")` (or `NewRWMutex`), then `queue.Lock(ctx)` ... `queue.Unlock(ctx)`:
  a `sync.Mutex` whose waits and holds show on ctx's track.
- Collections, the heap and the goroutine count are reported by themselves.
- Go's own tracer: `runtime/trace` (`go test -trace trace.out`, or `trace.Start`) writes an execution trace; Solution
  Explorer > Import into Profiler on the `.out` converts it with the workspace's Go toolchain: each P's goroutines,
  each goroutine's regions, tasks and logs, the collector, the heap.

## Instrumenting Rust

The `ied-profiler` crate (`plugins/rust/profiler`; a path or git dependency). The cargo preset's Profile task runs
`cargo run` with `IED_PROFILER`.

```rust
fn update(world: &World) {
    let _zone = ied_profiler::zone!("Update");        // bind it: `let _ =` drops it at once
    ied_profiler::plot("Entities", world.len() as f64);
}
// in the loop, once per presented frame
ied_profiler::frame();
```

- Threads keep their std names; `ied_profiler::set_thread_name` renames one. `Task::begin("Decode", None)` for tasks.
- Locks: `static QUEUE: ied_profiler::Mutex<Vec<Job>> = ied_profiler::mutex!("Queue", Vec::new());` (or
  `Mutex::new("Queue", value)`), then `QUEUE.lock()` as with a std mutex; `LockSite` reports a lock of your own.
- `#[global_allocator] static A: ied_profiler::Tracked<std::alloc::System> = ied_profiler::Tracked(std::alloc::System);`
  reports every allocation (Memory, leaks).
- Code already using `tracing`: enable the crate's `tracing` feature and
  `tracing::subscriber::set_global_default(ied_profiler::tracing::Subscriber::new())` - spans become zones, events
  messages.

## Other languages and tools

- **Chrome trace JSON** (Rust's `tracing-chrome`, Chrome and Perfetto exports, Nuke's `build-trace.json`, many engines):
  Solution Explorer > Import into Profiler on the `.json`, or `prof.import`. Complete and begin/end events become zones,
  counters plots, async events tasks.
- **pprof** profiles (Go: `go test -cpuprofile cpu.pprof -bench .`, `runtime/pprof`, `net/http/pprof`; Rust:
  `pprof-rs`; gperftools): import the file; `profile_flame` reads its samples as a call tree with each function's
  share (a sampled profile has no timeline or zones).
