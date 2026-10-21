// Package profiler is the Go client of the IED Trace Protocol (IED docs/16). A program run with IED_PROFILER set
// streams its zones, frames, plots, messages, goroutine tracks, tasks and the runtime's collections to the IED
// profiler (IED's Profile task sets it to "127.0.0.1:<port>;token=<t>"), or with "file:<path>" to a trace file that
// IED opens. Without it every call returns after one atomic load.
//
//	func main() {
//		defer profiler.Stop()
//		ctx := profiler.WithThread(context.Background(), "Main")
//		for running {
//			update(ctx)
//			profiler.Frame()
//		}
//	}
//
//	func update(ctx context.Context) {
//		defer profiler.Zone(ctx, "Update").End()
//		profiler.Plot("Entities", float64(len(world)))
//	}
//
// Each goroutine records on a track of its own, the profiler's "thread": the one WithThread puts in its context, or
// "goroutine N" made the first time it records without one (finding it costs about a microsecond, so give hot
// goroutines a context of their own). A context's track belongs to the goroutine that made it: a goroutine started
// with it calls WithThread again.
package profiler

import (
	"context"
	"runtime"
)

// Severity of a Message.
type Severity int

const (
	Trace Severity = iota
	Debug
	Info
	Warning
	Error
	Fatal
)

type trackKey struct{}
type taskKey struct{}

// Start records to target ("host:port;token=<t>" or "file:<path>") as app. A program run by IED does not call it:
// its first call starts from IED_PROFILER.
func Start(target, app string) error {
	running()
	return startSession(target, app)
}

// Stop sends what is buffered and ends the session; defer it in main, as Go runs nothing at exit.
func Stop() {
	startMu.Lock()
	defer startMu.Unlock()
	if !state.CompareAndSwap(stateOn, stateOff) {
		return
	}
	current.Load().stop()
}

// Active reports whether a session records.
func Active() bool { return running() != nil }

// WithThread gives ctx a track of its own named `name`, for the calling goroutine.
func WithThread(ctx context.Context, name string) context.Context {
	s := running()
	if s == nil {
		return ctx
	}
	return context.WithValue(ctx, trackKey{}, s.newTrack(name, goroutineID()))
}

// SetThreadName renames ctx's track (or the goroutine's) from now on: a pooled goroutine taking another role.
func SetThreadName(ctx context.Context, name string) {
	s := running()
	if s == nil {
		return
	}
	nameID := s.interned(name)
	at := now()
	s.write(s.trackOf(ctx), func(b *buffer) {
		b.timed(evThreadName, at)
		b.bytes = putVarint(b.bytes, nameID)
	})
}

func (s *session) trackOf(ctx context.Context) *buffer {
	if ctx != nil {
		if t, ok := ctx.Value(trackKey{}).(*buffer); ok && t.session == s.number {
			return t
		}
	}
	return s.goroutineTrack()
}

// --- Zones ------------------------------------------------------------------------------------------------------

// ZoneScope is an open zone; End closes it.
type ZoneScope struct {
	s     *session
	track *buffer
}

// Zone opens a zone named `name` on ctx's track, at the caller's file and line: defer profiler.Zone(ctx, "Name").End().
func Zone(ctx context.Context, name string) ZoneScope {
	s := running()
	if s == nil {
		return ZoneScope{}
	}
	var pcs [1]uintptr
	runtime.Callers(2, pcs[:])
	id, named := s.siteOf(pcs[0], name)
	track := s.trackOf(ctx)
	at := now()
	s.write(track, func(b *buffer) {
		if named == 0 {
			b.timed(evZoneBegin, at)
			b.bytes = putVarint(b.bytes, id)
			return
		}
		b.timed(evZoneBeginNamed, at)
		b.bytes = putVarint(b.bytes, id)
		b.bytes = putVarint(b.bytes, named)
	})
	return ZoneScope{s, track}
}

func (z ZoneScope) End() {
	if z.track == nil {
		return
	}
	at := now()
	z.s.write(z.track, func(b *buffer) { b.timed(evZoneEnd, at) })
}

// Text attaches text to the zone (shown in its details).
func (z ZoneScope) Text(text string) {
	if z.track == nil {
		return
	}
	id := z.s.onceText(text)
	z.s.write(z.track, func(b *buffer) {
		b.bytes = append(b.bytes, evZoneText)
		b.bytes = putVarint(b.bytes, id)
	})
}

// Value attaches a number to the zone.
func (z ZoneScope) Value(value uint64) {
	if z.track == nil {
		return
	}
	z.s.write(z.track, func(b *buffer) {
		b.bytes = append(b.bytes, evZoneValue)
		b.bytes = putVarint(b.bytes, value)
	})
}

// --- Frames, plots, messages ------------------------------------------------------------------------------------

// Frame marks the end of a frame of the default set.
func Frame() { frameEvent(evFrameMark, "") }

// FrameNamed marks the end of a frame of the set `set`.
func FrameNamed(set string) { frameEvent(evFrameMark, set) }

// FrameBegin and FrameEnd bound a discontinuous frame of the set `set` (a level load).
func FrameBegin(set string) { frameEvent(evFrameBegin, set) }
func FrameEnd(set string)   { frameEvent(evFrameEnd, set) }

func frameEvent(kind byte, set string) {
	s := running()
	if s == nil {
		return
	}
	setID := s.interned(set)
	at := now()
	s.writeGlobal(func(g *buffer) {
		g.timed(kind, at)
		g.bytes = putVarint(g.bytes, setID)
	})
}

// Plot records a value of the series `name`.
func Plot(name string, value float64) {
	s := running()
	if s == nil {
		return
	}
	s.plot(name, plotNumber, value, now())
}

func (s *session) plot(name string, kind uint64, value float64, at int64) {
	id := s.plotID(name, kind)
	s.writeGlobal(func(g *buffer) {
		g.timed(evPlotValue, at)
		g.bytes = putVarint(g.bytes, id)
		g.bytes = putDouble(g.bytes, value)
	})
}

// Message records a log line on ctx's track.
func Message(ctx context.Context, severity Severity, text string) {
	s := running()
	if s == nil {
		return
	}
	id := s.onceText(text)
	at := now()
	s.write(s.trackOf(ctx), func(b *buffer) {
		b.timed(evMessage, at)
		b.bytes = putVarint(b.bytes, uint64(max(severity, 0)))
		b.bytes = putVarint(b.bytes, id)
		b.bytes = putVarint(b.bytes, 0)
	})
}

// --- Tasks ------------------------------------------------------------------------------------------------------

// TaskScope is a unit of work the profiler's Tasks view groups by name, wherever it runs.
type TaskScope struct {
	s     *session
	id    uint64
	track *buffer
}

// Task begins a task named `name` on ctx's track, a child of ctx's task; the context carries it to children.
func Task(ctx context.Context, name string) (context.Context, *TaskScope) {
	s := running()
	if s == nil {
		return ctx, &TaskScope{}
	}
	var parent uint64
	if p, ok := ctx.Value(taskKey{}).(*TaskScope); ok && p.s == s {
		parent = p.id
	}
	task := &TaskScope{s: s, id: s.nextTask.Add(1), track: s.trackOf(ctx)}
	nameID := s.interned(name)
	at := now()
	s.write(task.track, func(b *buffer) {
		b.timed(evTaskBegin, at)
		b.bytes = putVarint(b.bytes, task.id)
		b.bytes = putVarint(b.bytes, nameID)
		b.bytes = putVarint(b.bytes, parent)
	})
	return context.WithValue(ctx, taskKey{}, task), task
}

// Suspend marks the task waiting; Resume continues it on ctx's track (another goroutine's, perhaps).
func (t *TaskScope) Suspend() { t.event(evTaskSuspend) }

func (t *TaskScope) Resume(ctx context.Context) {
	if t.s == nil {
		return
	}
	t.track = t.s.trackOf(ctx)
	t.event(evTaskResume)
}

// End ends the task on the track it last ran on.
func (t *TaskScope) End() { t.event(evTaskEnd) }

func (t *TaskScope) event(kind byte) {
	if t.s == nil || state.Load() != stateOn {
		return
	}
	at := now()
	t.s.write(t.track, func(b *buffer) {
		b.timed(kind, at)
		b.bytes = putVarint(b.bytes, t.id)
	})
}

// --- Pools ------------------------------------------------------------------------------------------------------

// Alloc and Free record a block of the program's own pool `pool` (an arena, a free list); PoolName names it.
func Alloc(ctx context.Context, address uintptr, size uint64, pool uint32) {
	memoryEvent(ctx, evAlloc, address, size, pool)
}

func Free(ctx context.Context, address uintptr, pool uint32) {
	memoryEvent(ctx, evFree, address, 0, pool)
}

func PoolName(pool uint32, name string) {
	s := running()
	if s == nil {
		return
	}
	nameID := s.interned(name)
	s.writeGlobal(func(g *buffer) {
		g.bytes = append(g.bytes, evPoolDef)
		g.bytes = putVarint(g.bytes, uint64(pool))
		g.bytes = putVarint(g.bytes, nameID)
	})
}

// memoryEvent goes to the global queue, so a block freed on another goroutine is never seen before its allocation.
func memoryEvent(ctx context.Context, kind byte, address uintptr, size uint64, pool uint32) {
	s := running()
	if s == nil {
		return
	}
	thread := s.trackOf(ctx).id
	at := now()
	s.writeGlobal(func(g *buffer) {
		g.timed(kind, at)
		g.bytes = putVarint(g.bytes, thread)
		g.bytes = putVarint(g.bytes, uint64(pool))
		g.bytes = putVarint(g.bytes, uint64(address))
		if kind == evAlloc {
			g.bytes = putVarint(g.bytes, size)
			g.bytes = putVarint(g.bytes, 0)
		}
	})
}
