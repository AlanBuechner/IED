package profiler

import (
	"syscall"
	"unsafe"
)

var (
	kernel32         = syscall.NewLazyDLL("kernel32.dll")
	queryCounter     = kernel32.NewProc("QueryPerformanceCounter")
	counterFrequency = readCounterFrequency()
)

func readCounterFrequency() int64 {
	var frequency int64
	kernel32.NewProc("QueryPerformanceFrequency").Call(uintptr(unsafe.Pointer(&frequency)))
	return max(frequency, 1)
}

// preciseNs is the system's performance counter in nanoseconds: Go's own clock on Windows moves only at the timer
// interrupt (about half a millisecond).
func preciseNs() int64 {
	var counter int64
	queryCounter.Call(uintptr(unsafe.Pointer(&counter)))
	return counter/counterFrequency*1_000_000_000 + counter%counterFrequency*1_000_000_000/counterFrequency
}
