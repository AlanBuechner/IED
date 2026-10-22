// The client's example, and the program test_itp_go.py runs: a game-like loop with zones on goroutines of their own
// and on one without a context, named zones with text and a value, frames, plots, a message, tasks, a pool, buffers
// allocated every frame (the runtime's allocation samples) and a collection. The target is the first argument ("file:<path>" or "host:port"); without one, IED_PROFILER.
package main

import (
	"context"
	"fmt"
	"os"
	"runtime"
	"sync"
	"time"
	"unsafe"

	"github.com/AlanBuechner/IED/plugins/go/profiler"
)

func main() {
	if len(os.Args) > 1 {
		if err := profiler.Start(os.Args[1], "itp-go-test"); err != nil {
			fmt.Println("start failed:", err)
			os.Exit(2)
		}
	}
	defer profiler.Stop()
	ctx := profiler.WithThread(context.Background(), "Main")
	profiler.FrameBegin("Loading")
	spin(ctx, 1000)
	profiler.FrameEnd("Loading")
	for frame := 0; frame < 5; frame++ {
		update(ctx, frame)
		profiler.Frame()
	}
	background()
	contend(ctx)
	profiler.Message(ctx, profiler.Warning, "done")
	pool(ctx)
	runtime.GC()
}

func update(ctx context.Context, frame int) {
	defer profiler.Zone(ctx, "Update").End()
	load := profiler.Zone(ctx, "Load")
	load.Text("bytes=4096")
	load.Value(4096)
	for _, file := range []string{"a.bin", "b.bin"} {
		zone := profiler.Zone(ctx, file)
		spin(ctx, 1000)
		zone.End()
	}
	decode()
	load.End()
	var done sync.WaitGroup
	done.Add(1)
	go func() {
		defer done.Done()
		physics(profiler.WithThread(context.Background(), "Worker"))
	}()
	done.Wait()
	profiler.Plot("Frame", float64(frame))
}

var decoded []byte

// decode reads a file into fresh buffers every frame: 1 MiB the runtime's allocation profile samples.
func decode() {
	for i := 0; i < 16; i++ {
		buffer := make([]byte, 64*1024)
		buffer[i] = byte(i)
		decoded = buffer
	}
}

func physics(ctx context.Context) {
	ctx, task := profiler.Task(ctx, "Physics")
	defer task.End()
	defer profiler.Zone(ctx, "Physics.Step").End()
	spin(ctx, 20000)
}

// background records from a goroutine with no track of its own: it gets "goroutine N".
func background() {
	var done sync.WaitGroup
	done.Add(1)
	go func() {
		defer done.Done()
		defer profiler.Zone(context.Background(), "Background").End()
		spin(context.Background(), 1000)
	}()
	done.Wait()
}

var queue = profiler.NewMutex("Queue")

// contend holds the queue while a goroutine waits for it, then takes it again without waiting.
func contend(ctx context.Context) {
	queue.Lock(ctx)
	waiting := make(chan struct{})
	var done sync.WaitGroup
	done.Add(1)
	go func() {
		defer done.Done()
		worker := profiler.WithThread(context.Background(), "Waiter")
		close(waiting)
		queue.Lock(worker)
		queue.Unlock(worker)
	}()
	<-waiting
	time.Sleep(5 * time.Millisecond)
	queue.Unlock(ctx)
	done.Wait()
	queue.Lock(ctx)
	queue.Unlock(ctx)
}

func pool(ctx context.Context) {
	block := new([64]byte)
	profiler.PoolName(2, "Arena")
	profiler.Alloc(ctx, uintptr(unsafe.Pointer(block)), uint64(len(block)), 2)
	profiler.Free(ctx, uintptr(unsafe.Pointer(block)), 2)
}

func spin(ctx context.Context, count int) int {
	defer profiler.Zone(ctx, "Spin").End()
	sum := 0
	for i := 0; i < count; i++ {
		sum += i
	}
	return sum
}
