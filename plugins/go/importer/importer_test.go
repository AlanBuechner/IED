package main

import (
	"context"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"runtime/trace"
	"strings"
	"sync"
	"testing"
	"time"
)

// The trace viewer opens its page with this test binary when a test converts: it must not run the tests again.
func TestMain(m *testing.M) {
	if startedAsBrowser() {
		return
	}
	os.Exit(m.Run())
}

func TestParsedLinesGiveGoroutineKindTimeAndFields(t *testing.T) {
	event, ok := parseEvent(`M=27252 P=15 G=1 Log Time=107461054668012 Task=1 Category="info" Message="a \"quoted\" word"`)
	if !ok || event.goroutine != 1 || event.kind != "Log" || event.time != 107461054668012 {
		t.Fatalf("parsed %+v", event)
	}
	if event.fields["Task"] != "1" || event.fields["Category"] != "info" || event.fields["Message"] != `a "quoted" word` {
		t.Fatalf("fields %v", event.fields)
	}
	if event, _ := parseEvent(`M=-1 P=-1 G=-1 Sync Time=5 N=1`); event.goroutine != 0 {
		t.Fatalf("no goroutine is %d", event.goroutine)
	}
	for _, line := range []string{"Stack=", "\truntime.main @ 0x1", ""} {
		if _, ok := parseEvent(line); ok {
			t.Errorf("%q parsed as an event", line)
		}
	}
}

func readLines(t *testing.T, lines ...string) *goroutineTracks {
	t.Helper()
	tracks, err := readGoroutineTracks(strings.NewReader(strings.Join(lines, "\n")))
	if err != nil {
		t.Fatal(err)
	}
	return tracks
}

func TestRegionsNestPerGoroutineInMicrosecondsFromTheFirstEvent(t *testing.T) {
	tracks := readLines(t,
		`M=1 P=0 G=-1 Sync Time=1000000`,
		`M=1 P=0 G=7 RegionBegin Time=1002000 Task=0 Type="outer"`,
		`M=1 P=0 G=7 RegionBegin Time=1003000 Task=0 Type="inner"`,
		`M=1 P=0 G=8 RegionBegin Time=1003500 Task=0 Type="other"`,
		`M=1 P=0 G=7 RegionEnd Time=1004000 Task=0 Type="inner"`,
		`M=1 P=0 G=7 RegionEnd Time=1009000 Task=0 Type="outer"`,
		`M=1 P=0 G=8 RegionEnd Time=1005500 Task=0 Type="other"`)
	var got []string
	for _, event := range tracks.events {
		got = append(got, event.Name+" "+strings.TrimRight(strings.TrimRight(jsonNumber(event.Time), "0"), ".")+"+"+jsonNumber(*event.Dur))
	}
	want := []string{"inner 3+1", "outer 2+7", "other 3.5+2"}
	if strings.Join(got, ", ") != strings.Join(want, ", ") {
		t.Fatalf("regions %v, want %v", got, want)
	}
	if len(tracks.order) != 2 || tracks.order[0] != 7 || tracks.order[1] != 8 {
		t.Fatalf("goroutines %v", tracks.order)
	}
}

func jsonNumber(f float64) string {
	data, _ := json.Marshal(f)
	return string(data)
}

func TestRegionsOpenAtEitherEndOfTheTraceAreClosedThere(t *testing.T) {
	tracks := readLines(t,
		`M=1 P=0 G=-1 Sync Time=1000`,
		`M=1 P=0 G=3 RegionEnd Time=3000 Task=0 Type="before"`,
		`M=1 P=0 G=3 RegionBegin Time=4000 Task=0 Type="after"`,
		`M=1 P=0 G=3 Metric Time=9000 Name="/gc/heap/goal:bytes" Value=Value{Uint64(1)}`)
	if len(tracks.events) != 2 {
		t.Fatalf("events %+v", tracks.events)
	}
	before, after := tracks.events[0], tracks.events[1]
	if before.Name != "before" || before.Time != 0 || *before.Dur != 2 || after.Name != "after" || after.Time != 3 || *after.Dur != 5 {
		t.Fatalf("regions %+v %+v", before, after)
	}
}

func TestTasksAndLogsGoOnTheirGoroutines(t *testing.T) {
	tracks := readLines(t,
		`M=1 P=0 G=-1 Sync Time=0`,
		`M=1 P=0 G=1 TaskBegin Time=1000 ID=4 Parent=18446744073709551615 Type="frame"`,
		`M=1 P=0 G=1 Log Time=2000 Task=4 Category="" Message="hello"`,
		`M=1 P=0 G=2 TaskEnd Time=3000 ID=4 Type="frame"`)
	var got []string
	for _, event := range tracks.events {
		got = append(got, event.Phase+" "+event.Name+" G"+jsonNumber(float64(event.Tid)))
	}
	if strings.Join(got, ", ") != "b frame G1, i hello G1, e frame G2" || tracks.events[0].ID != "4" {
		t.Fatalf("events %v", got)
	}
}

func TestOtherFilesAreRefused(t *testing.T) {
	path := filepath.Join(t.TempDir(), "notes.out")
	os.WriteFile(path, []byte("hello, world"), 0o644)
	if err := checkHeader(path); err == nil {
		t.Fatal("a text file passed as a trace")
	}
}

// A trace of this test's own goroutines, converted end to end through the go tool that runs the test.
func TestARealTraceConverts(t *testing.T) {
	goCommand, err := exec.LookPath("go")
	if err != nil {
		t.Skip("no go tool")
	}
	dir := t.TempDir()
	input, output := filepath.Join(dir, "trace.out"), filepath.Join(dir, "trace.json")
	record(t, input)
	if err := convert(goCommand, input, output); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(output)
	if err != nil {
		t.Fatal(err)
	}
	var converted chromeTrace
	if err := json.Unmarshal(data, &converted); err != nil {
		t.Fatal(err)
	}
	counts := map[string]int{}
	for _, event := range converted.TraceEvents {
		switch {
		case event.Pid == goroutinesPid && event.Phase == "X":
			counts["region "+event.Name]++
		case event.Pid == goroutinesPid && event.Phase != "M":
			counts[event.Phase+" "+event.Name]++
		case event.Phase == "C":
			counts["counter"]++
		case event.Phase == "I" && (event.Name == "proc start" || event.Name == "proc stop"):
			counts["noise"]++
		}
	}
	if counts["region work"] != 3 || counts["region background"] != 1 || counts["b frame"] != 1 || counts["e frame"] != 1 || counts["i [info] started"] != 1 {
		t.Fatalf("goroutine events %v", counts)
	}
	if counts["counter"] == 0 || counts["noise"] != 0 {
		t.Fatalf("viewer events %v", counts)
	}
	if first := converted.TraceEvents[0]; first.Name != "process_name" || first.Args["name"] != "trace" {
		t.Fatalf("the first event %+v does not name the program after the trace", first)
	}
}

func record(t *testing.T, path string) {
	t.Helper()
	file, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()
	if err := trace.Start(file); err != nil {
		t.Fatal(err)
	}
	defer trace.Stop()
	ctx, task := trace.NewTask(context.Background(), "frame")
	trace.Log(ctx, "info", "started")
	var done sync.WaitGroup
	for i := 0; i < 3; i++ {
		done.Add(1)
		go func() {
			defer done.Done()
			trace.WithRegion(ctx, "work", func() { time.Sleep(time.Millisecond) })
		}()
	}
	done.Wait()
	trace.WithRegion(context.Background(), "background", func() { time.Sleep(time.Millisecond) })
	task.End()
}
