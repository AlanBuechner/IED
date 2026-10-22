package profiler

import (
	"runtime"
	"strings"
)

const (
	sampleBytes     = 2          // Sample's kind: bytes allocated
	defaultSampling = 512 * 1024 // runtime.MemProfileRate as Go sets it
	sessionSampling = 64 * 1024  // the rate while a session runs, when the program kept the default
	runtimePackage  = "runtime."
	runtimeInternal = "internal/runtime/"
)

// ownPackage is this package's prefix in a function's name ("github.com/.../profiler."): its allocations are the
// profiler's, not the program's.
var ownPackage = func() string {
	pc, _, _, _ := runtime.Caller(0)
	name := runtime.FuncForPC(pc).Name()
	slash := strings.LastIndex(name, "/")
	return name[:slash+strings.Index(name[slash:], ".")+1]
}()

// allocSampler streams the runtime's own allocation profile (runtime.MemProfile) as Sample events: the bytes each call
// stack allocated since the last report. The runtime samples one allocation every MemProfileRate bytes and publishes
// them at each collection, so a report is as late as the last collection. The runtime's own frames (mallocgc,
// makeslice, growslice) are left off, so a stack's first frame is the program's code that allocated; the profiler's
// own allocations are not reported.
type allocSampler struct {
	records   []runtime.MemProfileRecord
	seen      map[[32]uintptr]int64
	stacks    map[[32]uintptr]uint64
	frames    map[string]uint64
	nextStack uint64
	nextFrame uint64
}

// begin takes what was allocated before the session as seen, and samples more often when the program left the rate.
func (a *allocSampler) begin() {
	if runtime.MemProfileRate == defaultSampling {
		runtime.MemProfileRate = sessionSampling
	}
	a.seen = map[[32]uintptr]int64{}
	a.stacks = map[[32]uintptr]uint64{}
	a.frames = map[string]uint64{}
	a.report(nil)
}

func (a *allocSampler) report(s *session) {
	n, ok := runtime.MemProfile(a.records, true)
	if !ok {
		a.records = make([]runtime.MemProfileRecord, n+n/4+16)
		if n, ok = runtime.MemProfile(a.records, true); !ok {
			return
		}
	}
	at := now()
	for _, record := range a.records[:n] {
		grown := record.AllocBytes - a.seen[record.Stack0]
		if grown <= 0 {
			continue
		}
		a.seen[record.Stack0] = record.AllocBytes
		if s == nil {
			continue
		}
		stack := a.stackID(s, record.Stack0, record.Stack())
		if stack == 0 {
			continue
		}
		s.writeGlobal(func(g *buffer) {
			g.timed(evSample, at)
			g.bytes = putVarint(g.bytes, 0) // the profile does not say which goroutine allocated
			g.bytes = putVarint(g.bytes, stack)
			g.bytes = putVarint(g.bytes, sampleBytes)
			g.bytes = putVarint(g.bytes, uint64(grown))
		})
	}
}

// stackID is the id of a call stack, its frames' SymbolDefs and its CallstackDef queued the first time; 0 for the
// profiler's own allocations.
func (a *allocSampler) stackID(s *session, key [32]uintptr, pcs []uintptr) uint64 {
	if id, ok := a.stacks[key]; ok {
		return id
	}
	var frames []runtime.Frame
	walk := runtime.CallersFrames(pcs)
	for {
		frame, more := walk.Next()
		frames = append(frames, frame)
		if !more {
			break
		}
	}
	first := 0
	for first < len(frames)-1 && isRuntime(frames[first].Function) {
		first++
	}
	if strings.HasPrefix(frames[first].Function, ownPackage) {
		a.stacks[key] = 0
		return 0
	}
	ids := make([]uint64, 0, len(frames)-first)
	for _, frame := range frames[first:] {
		ids = append(ids, a.frameID(s, frame))
	}
	a.nextStack++
	id := a.nextStack
	a.stacks[key] = id
	s.writeGlobal(func(g *buffer) {
		g.bytes = append(g.bytes, evCallstackDef)
		g.bytes = putVarint(g.bytes, id)
		g.bytes = putVarint(g.bytes, uint64(len(ids)))
		for _, frame := range ids {
			g.bytes = putVarint(g.bytes, frame)
		}
	})
	return id
}

func isRuntime(function string) bool {
	return strings.HasPrefix(function, runtimePackage) || strings.HasPrefix(function, runtimeInternal)
}

// frameID is a frame's synthetic address, one per function and line, named by a SymbolDef the first time.
func (a *allocSampler) frameID(s *session, frame runtime.Frame) uint64 {
	key := frame.Function + "\x00" + frame.File + "\x00" + itoa(uint64(frame.Line))
	if id, ok := a.frames[key]; ok {
		return id
	}
	a.nextFrame++
	id := a.nextFrame
	a.frames[key] = id
	function, file := s.interned(frame.Function), s.interned(frame.File)
	s.writeGlobal(func(g *buffer) {
		g.bytes = append(g.bytes, evSymbolDef)
		g.bytes = putVarint(g.bytes, id)
		g.bytes = putVarint(g.bytes, function)
		g.bytes = putVarint(g.bytes, file)
		g.bytes = putVarint(g.bytes, uint64(frame.Line))
	})
	return id
}
