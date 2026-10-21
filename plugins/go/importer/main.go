// Command importer turns a Go execution trace (runtime/trace.Start, go test -trace) into Chrome trace JSON for IED's
// profiler: the go plugin's profilerImporters entry runs it on a trace being imported. The toolchain that wrote a
// trace parses it: `go tool trace` gives each P's goroutines, the collector's phases and the runtime's counters (its
// viewer's JSON), and its parsed events give each goroutine's regions, tasks and logs.
//
//	go run . [-go <go command>] <trace.out> <out.json>
package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// openingViewer marks the trace viewer's environment: `go tool trace` opens its page with $BROWSER, set to this program
// (or to the test binary), which then has nothing to do. Started so, it must never convert again.
const openingViewer = "IED_GO_TRACE_VIEWER"

func startedAsBrowser() bool {
	return os.Getenv(openingViewer) != ""
}

func main() {
	if startedAsBrowser() {
		return
	}
	goCommand := flag.String("go", "go", "the go command")
	flag.Parse()
	if flag.NArg() != 2 {
		fmt.Fprintln(os.Stderr, "usage: importer [-go <go command>] <trace.out> <out.json>")
		os.Exit(2)
	}
	if err := convert(*goCommand, flag.Arg(0), flag.Arg(1)); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func convert(goCommand, input, output string) error {
	if err := checkHeader(input); err != nil {
		return err
	}
	procs, err := procView(goCommand, input)
	if err != nil {
		return err
	}
	goroutines, err := goroutineView(goCommand, input)
	if err != nil {
		return err
	}
	program := strings.TrimSuffix(filepath.Base(input), filepath.Ext(input))
	return writeTrace(output, merge(procs, goroutines, program))
}

// checkHeader refuses a file that is not a Go execution trace ("go 1.22 trace\x00...").
func checkHeader(input string) error {
	file, err := os.Open(input)
	if err != nil {
		return err
	}
	defer file.Close()
	head := make([]byte, 16)
	n, _ := io.ReadFull(file, head)
	if !strings.HasPrefix(string(head[:n]), "go 1.") || !strings.Contains(string(head[:n]), " trace") {
		return errors.New(input + " is not a Go execution trace")
	}
	return nil
}
