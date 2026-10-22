package profiler_test

import (
	"bytes"
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"github.com/AlanBuechner/IED/plugins/go/profiler"
)

var kept [][]byte

// allocateForTheTest allocates enough that the runtime's profile samples it.
func allocateForTheTest() {
	for i := 0; i < 256; i++ {
		kept = append(kept, make([]byte, 64*1024))
	}
}

func TestAllocationsAreSampledByTheFunctionThatMadeThem(t *testing.T) {
	path := filepath.Join(t.TempDir(), "allocs.iedtrace")
	if err := profiler.Start("file:"+path, "allocs"); err != nil {
		t.Fatal(err)
	}
	allocateForTheTest()
	runtime.GC()
	runtime.GC()
	profiler.Stop()
	kept = nil
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Contains(data, []byte("profiler_test.allocateForTheTest")) {
		t.Fatal("the stream names no sampled allocation in allocateForTheTest")
	}
	if bytes.Contains(data, []byte("runtime.mallocgc")) || bytes.Contains(data, []byte("profiler.putText")) {
		t.Fatal("the runtime's or the profiler's own frames were sent")
	}
}
