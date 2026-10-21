package profiler

import (
	"bytes"
	"context"
	"os"
	"path/filepath"
	"sync"
	"testing"
)

func TestVarintsTakeSevenBitGroups(t *testing.T) {
	cases := map[uint64][]byte{0: {0}, 127: {0x7f}, 128: {0x80, 0x01}, 1<<64 - 1: {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01}}
	for value, want := range cases {
		if got := putVarint(nil, value); !bytes.Equal(got, want) {
			t.Errorf("putVarint(%d) = %x, want %x", value, got, want)
		}
	}
}

func TestZigzagPutsSmallMagnitudesFirst(t *testing.T) {
	for value, want := range map[int64]byte{0: 0, -1: 1, 1: 2, -2: 3, 2: 4} {
		if got := putZigzag(nil, value); len(got) != 1 || got[0] != want {
			t.Errorf("putZigzag(%d) = %x, want %x", value, got, want)
		}
	}
}

func TestChunksCarryTheirLengthAndHeader(t *testing.T) {
	got := putChunk(nil, threadChunk, 3, 300, []byte{evZoneEnd, 2})
	want := []byte{6, 0, 0, 0, threadChunk, 3, 0xac, 0x02, evZoneEnd, 2}
	if !bytes.Equal(got, want) {
		t.Fatalf("putChunk = %x, want %x", got, want)
	}
}

func TestTakingABufferMovesItsBaseToItsLastEvent(t *testing.T) {
	b := &buffer{base: 100, last: 100}
	b.timed(evZoneBegin, 150)
	b.timed(evZoneEnd, 140)
	body, base, ok := b.take()
	if !ok || base != 100 || !bytes.Equal(body, []byte{evZoneBegin, 100, evZoneEnd, 19}) {
		t.Fatalf("take = %x %d %v", body, base, ok)
	}
	if _, _, ok := b.take(); ok {
		t.Fatal("an empty buffer gave events")
	}
	b.timed(evZoneEnd, 160)
	if _, base, _ := b.take(); base != 140 {
		t.Fatalf("the next take is from %d, want 140", base)
	}
}

func TestGoroutinesHaveIDsOfTheirOwn(t *testing.T) {
	mine := goroutineID()
	var other uint64
	var done sync.WaitGroup
	done.Add(1)
	go func() {
		defer done.Done()
		other = goroutineID()
	}()
	done.Wait()
	if mine == 0 || other == 0 || mine == other || goroutineID() != mine {
		t.Fatalf("goroutine ids %d and %d", mine, other)
	}
}

// Zones of microseconds need a clock finer than Go's own on Windows, which moves only at the timer interrupt.
func TestTheClockMeasuresMicroseconds(t *testing.T) {
	c := startClock()
	if c.frequency < 1_000_000 {
		t.Fatalf("%d ticks a second", c.frequency)
	}
	begin, precise := now(), preciseNs()
	for preciseNs()-precise < 200_000 {
	}
	elapsed := float64(now()-begin) / float64(c.frequency)
	if elapsed < 150e-6 || elapsed > 5e-3 {
		t.Fatalf("a 200 us wait measured %.1f us", elapsed*1e6)
	}
}

func TestMalformedTargetsAreRefused(t *testing.T) {
	for _, target := range []string{"no-port", "file:" + filepath.Join(t.TempDir(), "missing", "x.iedtrace")} {
		if _, _, err := open(target); err == nil {
			t.Errorf("open(%q) succeeded", target)
		}
	}
}

func TestASessionWritesAStreamThatEndsWithBye(t *testing.T) {
	path := filepath.Join(t.TempDir(), "run.iedtrace")
	if err := Start("file:"+path, "unit"); err != nil {
		t.Fatal(err)
	}
	if err := Start("file:"+path, "again"); err == nil {
		t.Error("a second session started while one runs")
	}
	ctx := WithThread(context.Background(), "Test")
	Zone(ctx, "Work").End()
	Frame()
	Stop()
	if Active() {
		t.Error("still active after Stop")
	}
	Zone(ctx, "After").End()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	bye := putChunk(nil, globalChunk, 0, 0, []byte{evBye, 0})
	if !bytes.HasPrefix(data, []byte("ITP1")) || !bytes.HasSuffix(data, bye[len(bye)-2:]) || bytes.Contains(data, []byte("After")) {
		t.Fatalf("the stream: %x", data)
	}
}
