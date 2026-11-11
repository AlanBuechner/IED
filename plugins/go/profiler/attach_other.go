//go:build !windows

package profiler

// IED's Attach to Process listens on a Windows named pipe (attach_windows.go); elsewhere there is nothing to attach to.
func listenForAttach(app string) {}
