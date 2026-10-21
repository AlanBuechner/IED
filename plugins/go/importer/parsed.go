package main

import (
	"bufio"
	"fmt"
	"io"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
)

// One event of `go tool trace -d=parsed`: "M=1 P=0 G=24 RegionBegin Time=107461054672379 Task=1 Type="work"".
type parsedEvent struct {
	goroutine uint64
	kind      string
	time      int64
	fields    map[string]string
}

var (
	eventHead  = regexp.MustCompile(`^M=-?\d+ P=-?\d+ G=(-?\d+) (\w+) Time=(\d+)(.*)$`)
	eventField = regexp.MustCompile(`(\w+)=("(?:[^"\\]|\\.)*"|\S+)`)
)

func parseEvent(line string) (parsedEvent, bool) {
	head := eventHead.FindStringSubmatch(line)
	if head == nil {
		return parsedEvent{}, false
	}
	goroutine, _ := strconv.ParseInt(head[1], 10, 64)
	time, _ := strconv.ParseInt(head[3], 10, 64)
	event := parsedEvent{kind: head[2], time: time, fields: map[string]string{}}
	if goroutine > 0 {
		event.goroutine = uint64(goroutine)
	}
	for _, field := range eventField.FindAllStringSubmatch(head[4], -1) {
		value := field[2]
		if unquoted, err := strconv.Unquote(value); err == nil {
			value = unquoted
		}
		event.fields[field[1]] = value
	}
	return event, true
}

// goroutineTracks are the Chrome events of each goroutine's own track: its regions as zones, the tasks it begins and
// ends, its logs; times in microseconds since the trace's first event, as the viewer's.
type goroutineTracks struct {
	events []chromeEvent
	order  []uint64
	seen   map[uint64]bool
	open   map[uint64][]parsedEvent
	origin int64
	begun  bool
}

func newGoroutineTracks() *goroutineTracks {
	return &goroutineTracks{seen: map[uint64]bool{}, open: map[uint64][]parsedEvent{}}
}

func (g *goroutineTracks) add(event parsedEvent) {
	if !g.begun {
		g.origin, g.begun = event.time, true
	}
	switch event.kind {
	case "RegionBegin":
		g.open[event.goroutine] = append(g.open[event.goroutine], event)
	case "RegionEnd":
		g.endRegion(event)
	case "TaskBegin":
		g.emit(event, chromeEvent{Name: event.fields["Type"], Phase: "b", Cat: "task", ID: event.fields["ID"]})
	case "TaskEnd":
		g.emit(event, chromeEvent{Name: event.fields["Type"], Phase: "e", Cat: "task", ID: event.fields["ID"]})
	case "Log":
		g.emit(event, chromeEvent{Name: logText(event), Phase: "i", Scope: "t"})
	}
}

// endRegion closes the goroutine's innermost open region of that type; a region begun before the trace started has
// no begin and starts with the trace.
func (g *goroutineTracks) endRegion(end parsedEvent) {
	open := g.open[end.goroutine]
	begin := parsedEvent{goroutine: end.goroutine, kind: "RegionBegin", time: g.origin, fields: end.fields}
	for i := len(open) - 1; i >= 0; i-- {
		if open[i].fields["Type"] == end.fields["Type"] {
			begin = open[i]
			g.open[end.goroutine] = append(open[:i:i], open[i+1:]...)
			break
		}
	}
	duration := g.micros(end.time) - g.micros(begin.time)
	g.emit(begin, chromeEvent{Name: begin.fields["Type"], Phase: "X", Dur: &duration, Cat: "region"})
}

// finish closes the regions still open when the trace ends.
func (g *goroutineTracks) finish(last int64) {
	for goroutine, open := range g.open {
		for i := len(open) - 1; i >= 0; i-- {
			g.endRegion(parsedEvent{goroutine: goroutine, kind: "RegionEnd", time: last, fields: open[i].fields})
		}
	}
}

func (g *goroutineTracks) emit(at parsedEvent, event chromeEvent) {
	if at.goroutine == 0 {
		return
	}
	if !g.seen[at.goroutine] {
		g.seen[at.goroutine] = true
		g.order = append(g.order, at.goroutine)
	}
	event.Time = g.micros(at.time)
	event.Pid = goroutinesPid
	event.Tid = at.goroutine
	g.events = append(g.events, event)
}

func (g *goroutineTracks) micros(t int64) float64 {
	return float64(t-g.origin) / 1000
}

func logText(event parsedEvent) string {
	if category := event.fields["Category"]; category != "" {
		return "[" + category + "] " + event.fields["Message"]
	}
	return event.fields["Message"]
}

func readGoroutineTracks(parsed io.Reader) (*goroutineTracks, error) {
	tracks := newGoroutineTracks()
	lines := bufio.NewScanner(parsed)
	lines.Buffer(make([]byte, 64*1024), 16*1024*1024)
	var last int64
	for lines.Scan() {
		if event, ok := parseEvent(lines.Text()); ok {
			tracks.add(event)
			last = event.time
		}
	}
	tracks.finish(last)
	return tracks, lines.Err()
}

// goroutineView reads `go tool trace -d=parsed`, the toolchain's own parse of the trace, event by event.
func goroutineView(goCommand, input string) (*goroutineTracks, error) {
	tool, err := traceTool(goCommand)
	if err != nil {
		return nil, err
	}
	parse := exec.Command(tool, "-d=parsed", input)
	out, err := parse.StdoutPipe()
	if err != nil {
		return nil, err
	}
	var problem strings.Builder
	parse.Stderr = &problem
	if err := parse.Start(); err != nil {
		return nil, fmt.Errorf("cannot run %s: %w", tool, err)
	}
	tracks, readErr := readGoroutineTracks(out)
	if err := parse.Wait(); err != nil {
		return nil, fmt.Errorf("go tool trace could not parse the trace: %v\n%s", err, problem.String())
	}
	return tracks, readErr
}
