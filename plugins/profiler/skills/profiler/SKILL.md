---
name: profiler
description: Find where a program spends its time and memory with IED's profiler - read captures with the profile_* tools, record one with profile_capture, instrument C4, C/C++, Go or Rust code, and import Chrome traces and pprof profiles.
keywords: [profile, profiler, performance, slow, frame time, hitch, stutter, spike, latency, flame graph, zones, capture, trace, pprof, leak, allocation, gc pause]
---

# Profiler

IED's profiler records **captures**: what an instrumented program did over time - zones (named spans of code) on each
thread, frames, plots, log messages, tasks, allocations, garbage collections. Captures live in the workspace's
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
9. After a change: record again and `profile_compare` the two captures (`a` before, `b` after).

Report findings with numbers (median and p95, not only the mean), the zone's source location, and what you would
change; open the source before proposing an edit.

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
- C: `ITP_ZONE_BEGIN("Name")` ... `ITP_ZONE_END()`.
- `ITP_ENABLE=0` compiles the `ITP_*` macros away; the functions stay and cost one load without `IED_PROFILER`.

## Other languages and tools

- **Chrome trace JSON** (Rust's `tracing-chrome`, Chrome and Perfetto exports, Nuke's `build-trace.json`, many engines):
  Solution Explorer > Import into Profiler on the `.json`, or `prof.import`. Complete and begin/end events become zones,
  counters plots, async events tasks.
- **pprof** profiles (Go: `go test -cpuprofile cpu.pprof -bench .`, `runtime/pprof`, `net/http/pprof`; Rust:
  `pprof-rs`; gperftools): import the file; `profile_flame` reads its samples as a call tree with each function's
  share (a sampled profile has no timeline or zones).
