package profiler

// IED's Attach to Process (IED docs/16 "Attaching to a running program"), as Bedrock's Profile/attach.cf and the C/C++
// client's Itp.cpp: a program started without IED_PROFILER listens on the local pipe ied-itp-<pid>, one line each way
// - hello ("itp-attach 1 <idle|attached> <app>"), attach <target> ("ok", "busy", "error <why>"; a target on this
// machine; the programs it starts after inherit it), detach ("ok").

import (
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"
	"unsafe"
)

var (
	createNamedPipe     = kernel32.NewProc("CreateNamedPipeW")
	connectNamedPipe    = kernel32.NewProc("ConnectNamedPipe")
	disconnectNamedPipe = kernel32.NewProc("DisconnectNamedPipe")
	attachListening     atomic.Bool
)

const (
	pipeAccessDuplex        = 0x3
	pipeTypeByte            = 0x0
	pipeReadModeByte        = 0x0
	pipeWait                = 0x0
	pipeRejectRemoteClients = 0x8
	pipeUnlimitedInstances  = 255
	errorPipeConnected      = 535
)

func isLocalTarget(target string) bool {
	return strings.HasPrefix(target, "127.0.0.1:") || strings.HasPrefix(target, "localhost:") || strings.HasPrefix(target, "[::1]:")
}

func attachAnswer(line, app string) string {
	switch {
	case line == "hello":
		if state.Load() == stateOn {
			return "itp-attach 1 attached " + app
		}
		return "itp-attach 1 idle " + app
	case strings.HasPrefix(line, "attach "):
		target := strings.TrimSpace(line[7:])
		if !isLocalTarget(target) {
			return "error the target is not on this machine"
		}
		if state.Load() == stateOn {
			return "busy"
		}
		if err := startSession(target, app); err != nil {
			return "error " + err.Error()
		}
		os.Setenv("IED_PROFILER", target)
		return "ok"
	case line == "detach":
		Stop()
		os.Unsetenv("IED_PROFILER")
		return "ok"
	}
	return "error not a request: hello, attach <target> or detach"
}

func attachLoop(app string) {
	name, err := syscall.UTF16PtrFromString(`\\.\pipe\ied-itp-` + strconv.Itoa(os.Getpid()))
	if err != nil {
		return
	}
	for {
		handle, _, _ := createNamedPipe.Call(uintptr(unsafe.Pointer(name)), pipeAccessDuplex,
			pipeTypeByte|pipeReadModeByte|pipeWait|pipeRejectRemoteClients, pipeUnlimitedInstances, 4096, 4096, 0, 0)
		pipe := syscall.Handle(handle)
		if pipe == syscall.InvalidHandle {
			return
		}
		ok, _, callErr := connectNamedPipe.Call(handle, 0)
		if ok == 0 && callErr != syscall.Errno(errorPipeConnected) {
			syscall.CloseHandle(pipe)
			continue
		}
		var line []byte
		buffer := make([]byte, 256)
		for len(line) < 4096 {
			var got uint32
			if syscall.ReadFile(pipe, buffer, &got, nil) != nil || got == 0 {
				break
			}
			line = append(line, buffer[:got]...)
			if strings.IndexByte(string(line), '\n') >= 0 {
				break
			}
		}
		request := strings.TrimSpace(strings.SplitN(string(line), "\n", 2)[0])
		if request != "" {
			answer := []byte(attachAnswer(request, app) + "\n")
			var wrote uint32
			syscall.WriteFile(pipe, answer, &wrote, nil)
			syscall.FlushFileBuffers(pipe)
		}
		disconnectNamedPipe.Call(handle)
		syscall.CloseHandle(pipe)
	}
}

// listenForAttach starts the attach pipe's listener, once.
func listenForAttach(app string) {
	if attachListening.Swap(true) {
		return
	}
	go attachLoop(app)
}
