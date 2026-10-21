package profiler

import (
	"errors"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	flushEvery     = 10 * time.Millisecond
	calibrateEvery = time.Second
	sampleEvery    = 10 // flushes between runtime samples (100 ms)
	dialTimeout    = 5 * time.Second
)

const (
	stateUnknown int32 = iota
	stateOff
	stateOn
)

// The one session of the process: started from IED_PROFILER on the first call, or by Start.
var (
	state    atomic.Int32
	current  atomic.Pointer[session]
	startMu  sync.Mutex
	sessions int64
)

// buffer holds a track's (or the global queue's) events since the flusher last took them; base is the time the
// first delta in bytes is from.
type buffer struct {
	mu      sync.Mutex
	bytes   []byte
	base    int64
	last    int64
	id      uint64
	session int64
	queued  bool
}

func (b *buffer) timed(kind byte, t int64) {
	b.bytes = append(b.bytes, kind)
	b.bytes = putZigzag(b.bytes, t-b.last)
	b.last = t
}

// take returns the events and the time their first delta is from; false when there are none.
func (b *buffer) take() ([]byte, int64, bool) {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.queued = false
	if len(b.bytes) == 0 {
		return nil, 0, false
	}
	body, base := b.bytes, b.base
	b.bytes = nil
	b.base = b.last
	return body, base, true
}

type site struct {
	id   uint64
	name string
}

type session struct {
	number int64
	start  int64
	clock  clock
	out    io.WriteCloser

	mu         sync.Mutex // strings, plots, counters and the goroutine map
	strings    map[string]uint64
	plots      map[string]uint64
	goroutines map[uint64]*buffer
	nextString uint64
	nextSite   uint64
	nextPlot   uint64
	nextThread uint64
	nextTask   atomic.Uint64
	sites      sync.Map // call site's pc -> *site

	dirtyMu sync.Mutex // the tracks with events since the last flush
	dirty   []*buffer

	global    buffer
	runtime   runtimeSampler
	calibrate int64
	failed    atomic.Bool
	done      chan struct{}
	finished  chan struct{}
}

// running is the session when one records, else nil; the first call starts one from IED_PROFILER.
func running() *session {
	switch state.Load() {
	case stateOn:
		return current.Load()
	case stateOff:
		return nil
	}
	if state.CompareAndSwap(stateUnknown, stateOff) {
		if target := os.Getenv("IED_PROFILER"); target != "" {
			startSession(target, programName())
		}
	}
	if state.Load() == stateOn {
		return current.Load()
	}
	return nil
}

func programName() string {
	name := filepath.Base(os.Args[0])
	return strings.TrimSuffix(name, filepath.Ext(name))
}

func startSession(target, app string) error {
	startMu.Lock()
	defer startMu.Unlock()
	if state.Load() == stateOn {
		return errors.New("a profiling session is running")
	}
	out, token, err := open(target)
	if err != nil {
		return err
	}
	sessions++
	c := startClock()
	s := &session{
		number:     sessions,
		start:      c.startTicks,
		clock:      c,
		out:        out,
		strings:    map[string]uint64{},
		plots:      map[string]uint64{},
		goroutines: map[uint64]*buffer{},
		done:       make(chan struct{}),
		finished:   make(chan struct{}),
	}
	s.global.base, s.global.last = s.start, s.start
	s.runtime.begin()
	if _, err := out.Write(s.hello(app, token)); err != nil {
		out.Close()
		return errors.New("the profiler closed the connection")
	}
	s.queueCalibration(s.start)
	s.calibrate = s.start
	current.Store(s)
	state.Store(stateOn)
	go s.loop()
	return nil
}

// open connects to "host:port;token=<t>" or creates the file of "file:<path>".
func open(target string) (io.WriteCloser, string, error) {
	if path, ok := strings.CutPrefix(target, "file:"); ok {
		f, err := os.Create(path)
		if err != nil {
			return nil, "", errors.New("cannot write " + path)
		}
		return f, "", nil
	}
	address, token, _ := strings.Cut(target, ";token=")
	if _, _, err := net.SplitHostPort(address); err != nil {
		return nil, "", errors.New("not an address: " + address)
	}
	conn, err := net.DialTimeout("tcp", address, dialTimeout)
	if err != nil {
		return nil, "", errors.New("cannot reach the profiler at " + address)
	}
	return conn, token, nil
}

func (s *session) hello(app, token string) []byte {
	body := []byte{evHello}
	body = putVarint(body, protocolVersion)
	body = putText(body, app)
	body = putVarint(body, uint64(os.Getpid()))
	body = putVarint(body, uint64(s.clock.frequency))
	body = putVarint(body, uint64(s.start))
	body = putVarint(body, uint64(s.clock.startUnix))
	body = putVarint(body, estimatedFlag(s.clock))
	body = putText(body, token)
	return putChunk(preamble(), globalChunk, 0, s.start, body)
}

func (s *session) queueCalibration(t int64) {
	s.global.mu.Lock()
	defer s.global.mu.Unlock()
	s.global.bytes = append(s.global.bytes, evCalibration)
	s.global.bytes = putVarint(s.global.bytes, uint64(t))
	s.global.bytes = putVarint(s.global.bytes, uint64(s.clock.unixNs()))
}

// estimatedFlag is the Hello's flag for a clock whose rate the client measured.
func estimatedFlag(c clock) uint64 {
	if c.estimated {
		return 1
	}
	return 0
}

func (s *session) loop() {
	defer close(s.finished)
	ticker := time.NewTicker(flushEvery)
	defer ticker.Stop()
	for n := 0; ; n++ {
		select {
		case <-s.done:
			return
		case <-ticker.C:
		}
		if n%sampleEvery == 0 {
			s.runtime.sample(s)
		}
		if !s.flush() {
			s.failed.Store(true)
			state.CompareAndSwap(stateOn, stateOff)
			s.out.Close()
			return
		}
	}
}

// flush sends the global queue's chunk, then each track's; false when the send failed.
func (s *session) flush() bool {
	t := now()
	if t-s.calibrate >= int64(float64(s.clock.frequency)*calibrateEvery.Seconds()) {
		s.calibrate = t
		s.queueCalibration(t)
	}
	s.dirtyMu.Lock()
	tracks := s.dirty
	s.dirty = nil
	s.dirtyMu.Unlock()
	var threads []byte
	for _, track := range tracks {
		if body, base, ok := track.take(); ok {
			threads = putChunk(threads, threadChunk, track.id, base, body)
		}
	}
	var out []byte
	if body, base, ok := s.global.take(); ok {
		out = putChunk(out, globalChunk, 0, base, body)
	}
	out = append(out, threads...)
	if len(out) == 0 {
		return true
	}
	_, err := s.out.Write(out)
	return err == nil
}

// stop sends what is buffered and a Bye, then closes the connection; the state is already off.
func (s *session) stop() {
	close(s.done)
	<-s.finished
	if s.failed.Load() {
		return
	}
	s.runtime.sample(s)
	if s.flush() {
		s.out.Write(putChunk(nil, globalChunk, 0, s.start, []byte{evBye, 0}))
	}
	s.out.Close()
}

// --- Tracks and definitions -------------------------------------------------------------------------------------

// write runs fill on track t under its lock, queueing it for the next flush.
func (s *session) write(t *buffer, fill func(b *buffer)) {
	t.mu.Lock()
	fill(t)
	if !t.queued {
		t.queued = true
		s.dirtyMu.Lock()
		s.dirty = append(s.dirty, t)
		s.dirtyMu.Unlock()
	}
	t.mu.Unlock()
}

func (s *session) writeGlobal(fill func(b *buffer)) {
	s.global.mu.Lock()
	fill(&s.global)
	s.global.mu.Unlock()
}

// newTrack is a track named `name` for goroutine `goroutine`: its ThreadDef queued, its name from now on.
func (s *session) newTrack(name string, goroutine uint64) *buffer {
	s.mu.Lock()
	s.nextThread++
	t := &buffer{id: s.nextThread, session: s.number, base: s.start, last: s.start}
	nameID := s.intern(name)
	s.mu.Unlock()
	s.writeGlobal(func(g *buffer) {
		g.bytes = append(g.bytes, evThreadDef)
		g.bytes = putVarint(g.bytes, t.id)
		g.bytes = putVarint(g.bytes, goroutine)
		g.bytes = putVarint(g.bytes, 0)
	})
	at := now()
	s.write(t, func(b *buffer) {
		b.timed(evThreadName, at)
		b.bytes = putVarint(b.bytes, nameID)
	})
	return t
}

// goroutineTrack is the calling goroutine's own track, made the first time it records without one.
func (s *session) goroutineTrack() *buffer {
	g := goroutineID()
	s.mu.Lock()
	t, ok := s.goroutines[g]
	s.mu.Unlock()
	if ok {
		return t
	}
	t = s.newTrack("goroutine "+itoa(g), g)
	s.mu.Lock()
	s.goroutines[g] = t
	s.mu.Unlock()
	return t
}

// intern is the id of text, its StringDef queued the first time; s.mu is held.
func (s *session) intern(text string) uint64 {
	if text == "" {
		return 0
	}
	if id, ok := s.strings[text]; ok {
		return id
	}
	id := s.once(text)
	s.strings[text] = id
	return id
}

// once is a fresh id for text sent once (a message, a zone's text); s.mu is held.
func (s *session) once(text string) uint64 {
	s.nextString++
	id := s.nextString
	s.writeGlobal(func(g *buffer) {
		g.bytes = append(g.bytes, evStringDef)
		g.bytes = putVarint(g.bytes, id)
		g.bytes = putText(g.bytes, text)
	})
	return id
}

func (s *session) interned(text string) uint64 {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.intern(text)
}

func (s *session) onceText(text string) uint64 {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.once(text)
}

// siteOf registers the call site at pc as `name`'s source location; named is the per-call name's id when this call
// names its zone differently from the site's first call (0 otherwise).
func (s *session) siteOf(pc uintptr, name string) (id uint64, named uint64) {
	if found, ok := s.sites.Load(pc); ok {
		known := found.(*site)
		if known.name == name {
			return known.id, 0
		}
		return known.id, s.interned(name)
	}
	function, file, line := callSite(pc)
	s.mu.Lock()
	s.nextSite++
	made := &site{id: s.nextSite, name: name}
	nameID, functionID, fileID := s.intern(name), s.intern(function), s.intern(file)
	s.mu.Unlock()
	s.writeGlobal(func(g *buffer) {
		g.bytes = append(g.bytes, evSrcLocDef)
		g.bytes = putVarint(g.bytes, made.id)
		g.bytes = putVarint(g.bytes, nameID)
		g.bytes = putVarint(g.bytes, functionID)
		g.bytes = putVarint(g.bytes, fileID)
		g.bytes = putVarint(g.bytes, uint64(line))
		g.bytes = putVarint(g.bytes, 0)
	})
	if _, raced := s.sites.LoadOrStore(pc, made); raced {
		return s.siteOf(pc, name)
	}
	return made.id, 0
}

// plotID is the id of the series `name`, its PlotDef (kind 0 a number, 1 bytes) queued the first time.
func (s *session) plotID(name string, kind uint64) uint64 {
	s.mu.Lock()
	defer s.mu.Unlock()
	if id, ok := s.plots[name]; ok {
		return id
	}
	s.nextPlot++
	id := s.nextPlot
	s.plots[name] = id
	nameID := s.intern(name)
	s.writeGlobal(func(g *buffer) {
		g.bytes = append(g.bytes, evPlotDef)
		g.bytes = putVarint(g.bytes, id)
		g.bytes = putVarint(g.bytes, nameID)
		g.bytes = putVarint(g.bytes, kind)
		g.bytes = putVarint(g.bytes, 0)
	})
	return id
}

func itoa(v uint64) string {
	var digits [20]byte
	i := len(digits)
	for {
		i--
		digits[i] = byte('0' + v%10)
		v /= 10
		if v == 0 {
			return string(digits[i:])
		}
	}
}
