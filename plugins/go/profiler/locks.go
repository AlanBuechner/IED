package profiler

import (
	"context"
	"runtime"
	"sync"
	"sync/atomic"
)

// lockSite is a lock as the profiler shows it: its name and where it was made, registered once per session.
type lockSite struct {
	name    string
	file    string
	line    int
	id      atomic.Uint64
	session atomic.Int64
}

func newLockSite(name string) lockSite {
	_, file, line, _ := runtime.Caller(2)
	return lockSite{name: name, file: file, line: line}
}

// Mutex is a sync.Mutex the profiler watches: its waits and holds show on each track's lock rows, its contention in
// the Locks window. A free mutex is taken without recording a wait. Each call takes the context whose track it is on.
type Mutex struct {
	mu   sync.Mutex
	site lockSite
}

// NewMutex makes a mutex named `name`, at the caller's file and line.
func NewMutex(name string) *Mutex {
	return &Mutex{site: newLockSite(name)}
}

func (m *Mutex) Lock(ctx context.Context) {
	if !m.mu.TryLock() {
		lockEvent(ctx, evLockWait, &m.site, false)
		m.mu.Lock()
	}
	lockEvent(ctx, evLockObtain, &m.site, false)
}

func (m *Mutex) TryLock(ctx context.Context) bool {
	if !m.mu.TryLock() {
		return false
	}
	lockEvent(ctx, evLockObtain, &m.site, false)
	return true
}

func (m *Mutex) Unlock(ctx context.Context) {
	lockEvent(ctx, evLockRelease, &m.site, false)
	m.mu.Unlock()
}

// RWMutex is a sync.RWMutex the profiler watches; a reader's holds are shared.
type RWMutex struct {
	mu   sync.RWMutex
	site lockSite
}

// NewRWMutex makes a reader-writer mutex named `name`, at the caller's file and line.
func NewRWMutex(name string) *RWMutex {
	return &RWMutex{site: newLockSite(name)}
}

func (m *RWMutex) Lock(ctx context.Context) {
	if !m.mu.TryLock() {
		lockEvent(ctx, evLockWait, &m.site, false)
		m.mu.Lock()
	}
	lockEvent(ctx, evLockObtain, &m.site, false)
}

func (m *RWMutex) Unlock(ctx context.Context) {
	lockEvent(ctx, evLockRelease, &m.site, false)
	m.mu.Unlock()
}

func (m *RWMutex) RLock(ctx context.Context) {
	if !m.mu.TryRLock() {
		lockEvent(ctx, evLockWait, &m.site, true)
		m.mu.RLock()
	}
	lockEvent(ctx, evLockObtain, &m.site, true)
}

func (m *RWMutex) RUnlock(ctx context.Context) {
	lockEvent(ctx, evLockRelease, &m.site, true)
	m.mu.RUnlock()
}

// lockEvent is an event of the lock on ctx's track: its field is the lock's id times two, plus one for a shared hold.
func lockEvent(ctx context.Context, kind byte, site *lockSite, shared bool) {
	s := running()
	if s == nil {
		return
	}
	field := s.lockID(site) * 2
	if shared {
		field++
	}
	at := now()
	s.write(s.trackOf(ctx), func(b *buffer) {
		b.timed(kind, at)
		b.bytes = putVarint(b.bytes, field)
	})
}

// lockID is the lock's id in this session, its place and its LockDef queued the first time.
func (s *session) lockID(site *lockSite) uint64 {
	if site.session.Load() == s.number {
		return site.id.Load()
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if site.session.Load() == s.number {
		return site.id.Load()
	}
	nameID, fileID := s.intern(site.name), s.intern(site.file)
	s.nextSite++
	srcLoc := s.nextSite
	s.nextLock++
	id := s.nextLock
	s.writeGlobal(func(g *buffer) {
		g.bytes = append(g.bytes, evSrcLocDef)
		for _, field := range []uint64{srcLoc, nameID, 0, fileID, uint64(site.line), 0} {
			g.bytes = putVarint(g.bytes, field)
		}
		g.bytes = append(g.bytes, evLockDef)
		g.bytes = putVarint(g.bytes, id)
		g.bytes = putVarint(g.bytes, nameID)
		g.bytes = putVarint(g.bytes, srcLoc)
	})
	site.id.Store(id)
	site.session.Store(s.number)
	return id
}
