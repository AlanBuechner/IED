package main

import (
	"encoding/json"
	"os"
	"regexp"
	"strconv"
)

// The Chrome trace event format, as the trace viewer writes it and IED's profiler reads it.
type chromeTrace struct {
	TraceEvents []chromeEvent `json:"traceEvents"`
}

type chromeEvent struct {
	Name  string         `json:"name"`
	Phase string         `json:"ph"`
	Time  float64        `json:"ts"`
	Dur   *float64       `json:"dur,omitempty"`
	Pid   uint64         `json:"pid"`
	Tid   uint64         `json:"tid"`
	ID    any            `json:"id,omitempty"`
	Cat   string         `json:"cat,omitempty"`
	Scope string         `json:"s,omitempty"`
	Args  map[string]any `json:"args,omitempty"`
}

// goroutinesPid is the process the goroutine tracks go under, beside the viewer's PROCS (0), STATS (1) and TASKS (2).
const goroutinesPid = 3

var goroutineLabel = regexp.MustCompile(`^G(\d+) (.+)$`)

// merge is the viewer's events less the scheduler's noise (P start and stop instants, flow arrows, the logs it shows
// on Ps), then each goroutine's track, named as the viewer names its slices ("G21 main.worker"). The goroutines'
// process comes first, named after the program: a trace names none, and the first process names the capture.
func merge(procs *chromeTrace, goroutines *goroutineTracks, program string) *chromeTrace {
	labels := map[uint64]string{}
	out := chromeTrace{TraceEvents: []chromeEvent{{Name: "process_name", Phase: "M", Pid: goroutinesPid, Args: map[string]any{"name": program}}}}
	for _, event := range procs.TraceEvents {
		if match := goroutineLabel.FindStringSubmatch(event.Name); match != nil && event.Phase == "X" {
			id, _ := strconv.ParseUint(match[1], 10, 64)
			labels[id] = event.Name
		}
		if !schedulerNoise(event) {
			out.TraceEvents = append(out.TraceEvents, event)
		}
	}
	for _, id := range goroutines.order {
		name := labels[id]
		if name == "" {
			name = "G" + strconv.FormatUint(id, 10)
		}
		out.TraceEvents = append(out.TraceEvents, chromeEvent{Name: "thread_name", Phase: "M", Pid: goroutinesPid, Tid: id, Args: map[string]any{"name": name}})
	}
	out.TraceEvents = append(out.TraceEvents, goroutines.events...)
	return &out
}

func schedulerNoise(event chromeEvent) bool {
	switch event.Phase {
	case "s", "t", "f":
		return true
	case "I", "i":
		return event.Name == "proc start" || event.Name == "proc stop" || event.Cat == "user event"
	}
	return false
}

func writeTrace(output string, trace *chromeTrace) error {
	file, err := os.Create(output)
	if err != nil {
		return err
	}
	if err := json.NewEncoder(file).Encode(trace); err != nil {
		file.Close()
		return err
	}
	return file.Close()
}
