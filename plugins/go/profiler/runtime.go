package profiler

import (
	"runtime"
	"runtime/debug"
	"runtime/metrics"
	"strings"
)

const (
	heapPlot       = "Go heap"
	goroutinesPlot = "Goroutines"
	gcFull         = 2 // GcBegin's kind: Go's collector is not generational
	plotNumber     = 0
	plotBytes      = 1
)

var runtimeMetrics = []string{"/memory/classes/heap/objects:bytes", "/sched/goroutines:goroutines", "/gc/heap/live:bytes"}

// runtimeSampler reports the Go runtime ten times a second: each collection's stop-the-world pauses as a GcBegin and
// GcEnd (the runtime keeps when each ended and how long it took), and the heap and goroutine counts as plots.
type runtimeSampler struct {
	seenGCs int64
	stats   debug.GCStats
	samples []metrics.Sample
}

// begin takes the collections so far as seen: a session reports those after its start.
func (r *runtimeSampler) begin() {
	r.samples = make([]metrics.Sample, len(runtimeMetrics))
	for i, name := range runtimeMetrics {
		r.samples[i].Name = name
	}
	debug.ReadGCStats(&r.stats)
	r.seenGCs = r.stats.NumGC
}

func (r *runtimeSampler) sample(s *session) {
	metrics.Read(r.samples)
	at := now()
	s.plot(heapPlot, plotBytes, float64(uintValue(r.samples[0])), at)
	s.plot(goroutinesPlot, plotNumber, float64(uintValue(r.samples[1])), at)
	r.collections(s, uintValue(r.samples[2]))
}

// collections sends the pauses since the last sample, oldest first; the newest gets the live heap it left.
func (r *runtimeSampler) collections(s *session, live uint64) {
	debug.ReadGCStats(&r.stats)
	fresh := int(min(r.stats.NumGC-r.seenGCs, int64(len(r.stats.Pause))))
	r.seenGCs = r.stats.NumGC
	for i := fresh - 1; i >= 0; i-- {
		after := uint64(0)
		if i == 0 {
			after = live
		}
		end := s.clock.ticksAt(r.stats.PauseEnd[i])
		s.collection(end-int64(float64(r.stats.Pause[i])*float64(s.clock.frequency)/1e9), end, after)
	}
}

func (s *session) collection(begin, end int64, after uint64) {
	s.writeGlobal(func(g *buffer) {
		g.timed(evGcBegin, begin)
		g.bytes = putVarint(g.bytes, gcFull)
		g.bytes = putVarint(g.bytes, 0)
		g.timed(evGcEnd, end)
		g.bytes = putVarint(g.bytes, 0)
		g.bytes = putVarint(g.bytes, after)
		g.bytes = putVarint(g.bytes, 0)
		g.bytes = putVarint(g.bytes, 0)
		g.bytes = putVarint(g.bytes, 0)
	})
}

func uintValue(sample metrics.Sample) uint64 {
	if sample.Value.Kind() != metrics.KindUint64 {
		return 0
	}
	return sample.Value.Uint64()
}

// goroutineID is the calling goroutine's id, read from the head of its stack ("goroutine 18 [running]:").
func goroutineID() uint64 {
	var room [64]byte
	head := string(room[:runtime.Stack(room[:], false)])
	head = strings.TrimPrefix(head, "goroutine ")
	var id uint64
	for _, c := range head {
		if c < '0' || c > '9' {
			break
		}
		id = id*10 + uint64(c-'0')
	}
	return id
}

// callSite is the function, file and line of the call at pc.
func callSite(pc uintptr) (string, string, int) {
	frame, _ := runtime.CallersFrames([]uintptr{pc}).Next()
	return frame.Function, frame.File, frame.Line
}
