"""The scale test's program (assets/ScaleTest/generate.py) in Go, for the build-speed comparison: one package per
module (a record, a ledger over a slice and a map, a generic, a heap-allocated chain, twelve arithmetic steps and a
Work that calls the module before it), and a main that checks every module's Work on several goroutines.

    python golang.py <output folder> [modules]
"""
import os
import sys

STEPS = 12


def step(i, k):
    mul = 6364136223846793005 + 2 * (i * STEPS + k)
    add = 1442695040888963407 + i * 977 + k * 131
    rounds = 3 + (i + k) % 5
    return f'''func Step{k}(x uint64) uint64 {{
	acc := x ^ uint64({add})
	for r := 0; r < {rounds}; r++ {{
		acc = acc*uint64({mul}) + uint64(r)
		if acc&7 == {k % 8} {{
			acc = acc ^ (acc >> 13)
		}} else {{
			acc = acc + {(i * 7 + k) % 1000 + 1}
		}}
	}}
	switch acc % 3 {{
	case 0:
		acc = acc ^ {i + 1}
	case 1:
		acc = acc + (acc >> 7)
	default:
		acc = acc * 3
	}}
	return acc
}}
'''


def module(i):
    previous = f'\tprev "scale/m{i - 1}"\n' if i > 0 else ''
    steps = '\n'.join(step(i, k) for k in range(STEPS))
    adds = '\n'.join(f'\tledger.Add(Step{k}(seed + {k}))' for k in range(STEPS))
    recurse = '\tif seed > 64 {\n\t\tsum = sum + prev.Work(seed / 2)\n\t}\n' if i > 0 else ''
    return f'''// Module {i} of the build-speed comparison (Benchmarks/buildspeed/golang.py writes it).
package m{i}

import (
{previous}	"strconv"
	"strings"
)

type Record struct {{
	ID    int64
	Value uint64
	Label string
}}

type Holder struct {{
	Value uint64
	Next  *Holder
}}

type Ledger struct {{
	records []Record
	index   map[string]int64
	seed    uint64
}}

func NewLedger(seed uint64) *Ledger {{ return &Ledger{{index: map[string]int64{{}}, seed: seed}} }}

func (l *Ledger) Add(value uint64) {{
	record := Record{{ID: int64(len(l.records)), Value: value}}
	var label strings.Builder
	label.WriteString("m{i}-")
	label.WriteString(strconv.FormatUint(value%1000, 10))
	record.Label = label.String()
	l.index[record.Label] = record.ID
	l.records = append(l.records, record)
}}

func (l *Ledger) Total() uint64 {{
	total := l.seed
	for _, record := range l.records {{
		total = (total * 1099511628211) ^ record.Value
	}}
	return total
}}

func (l *Ledger) Describe() string {{
	var text strings.Builder
	for _, record := range l.records {{
		text.WriteString(record.Label)
		text.WriteString(";")
	}}
	return text.String()
}}

func (l *Ledger) Count() int64 {{ return int64(len(l.records)) }}

func (l *Ledger) Has(label string) bool {{
	_, ok := l.index[label]
	return ok
}}

func Mix[T uint64 | uint32](a, b T) T {{ return (a^b)*T({2 * i + 1}) + T({i}) }}

{steps}
// Work runs every step into a ledger, its text, two instances of the generic, a heap chain, and the module before.
func Work(seed uint64) uint64 {{
	ledger := NewLedger(seed)
{adds}
	sum := ledger.Total()
	text := ledger.Describe()
	sum = sum + uint64(len(text))
	sum = Mix[uint64](sum, uint64(ledger.Count()))
	sum = sum + uint64(Mix[uint32](uint32(sum), uint32({i})))
	if ledger.Has("m{i}-0") {{
		sum = sum ^ 1
	}}
	var chain *Holder
	for k := 0; k < 4; k++ {{
		chain = &Holder{{Value: sum + uint64(k), Next: chain}}
	}}
	for held := chain; held != nil; held = held.Next {{
		sum = sum ^ (held.Value >> 3)
	}}
{recurse}	return sum
}}
'''


def main_source(modules):
    imports = '\n'.join(f'\t"scale/m{i}"' for i in range(modules))
    calls = '\n'.join(f'\tsum = sum*31 + m{i}.Work(seed)' for i in range(modules))
    return f'''// The build-speed comparison's entry: every module's Work on SCALE_THREADS goroutines for SCALE_SECONDS seconds,
// each result checked against the one computed first. Exit code: the wrong results.
package main

import (
	"fmt"
	"os"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

{imports}
)

func runAll(seed uint64) uint64 {{
	sum := uint64(0)
{calls}
	return sum
}}

func setting(name string, fallback int64) int64 {{
	value, err := strconv.ParseInt(os.Getenv(name), 10, 64)
	if err != nil {{
		return fallback
	}}
	return value
}}

func main() {{
	seconds := setting("SCALE_SECONDS", 5)
	threads := setting("SCALE_THREADS", 4)
	var expected [8]uint64
	for s := range expected {{
		expected[s] = runAll(12345 + uint64(s))
	}}
	var runs, wrong atomic.Int64
	start := time.Now()
	deadline := start.Add(time.Duration(seconds) * time.Second)
	var workers sync.WaitGroup
	for t := int64(0); t < threads; t++ {{
		workers.Add(1)
		go func(i int64) {{
			defer workers.Done()
			for time.Now().Before(deadline) {{
				s := i % 8
				if runAll(12345+uint64(s)) != expected[s] {{
					wrong.Add(1)
				}}
				runs.Add(1)
				i++
			}}
		}}(t)
	}}
	workers.Wait()
	fmt.Printf("scale: %d runs of {modules} modules on %d goroutines in %d ms, %d wrong\\n", runs.Load(), threads, time.Since(start).Milliseconds(), wrong.Load())
	os.Exit(int(wrong.Load()))
}}
'''


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def generate(folder, modules):
    lines = 0
    write(os.path.join(folder, 'go.mod'), 'module scale\n\ngo 1.22\n')
    for i in range(modules):
        text = module(i)
        write(os.path.join(folder, f'm{i}', f'm{i}.go'), text)
        lines += text.count('\n')
    text = main_source(modules)
    write(os.path.join(folder, 'main.go'), text)
    return lines + text.count('\n')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    print(f'{generate(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 200)} lines')
