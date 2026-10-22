package main

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
)

const (
	exitBadLine    = 1
	exitNoCommands = 2
)

func runCommand(set *SpanSet, line string, out *bufio.Writer) bool {
	words := strings.Fields(line)
	if len(words) == 0 {
		return true
	}
	number := func(i int) (int64, bool) {
		v, err := strconv.ParseInt(words[i], 10, 64)
		return v, err == nil
	}
	switch {
	case (words[0] == "add" || words[0] == "remove") && len(words) == 3:
		a, okA := number(1)
		b, okB := number(2)
		if !okA || !okB {
			return false
		}
		if words[0] == "add" {
			set.Add(a, b)
		} else {
			set.Remove(a, b)
		}
	case words[0] == "contains" && len(words) == 2:
		x, ok := number(1)
		if !ok {
			return false
		}
		if set.Contains(x) {
			out.WriteString("yes\n")
		} else {
			out.WriteString("no\n")
		}
	case words[0] == "count" && len(words) == 1:
		fmt.Fprintln(out, set.Count())
	case words[0] == "covered" && len(words) == 1:
		fmt.Fprintln(out, set.Covered())
	case words[0] == "list" && len(words) == 1:
		parts := make([]string, 0, set.Count())
		for _, span := range set.All() {
			parts = append(parts, fmt.Sprintf("[%d,%d)", span.Start, span.End))
		}
		out.WriteString(strings.Join(parts, " ") + "\n")
	default:
		return false
	}
	return true
}

func run(args []string) int {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "error: cannot read commands")
		return exitNoCommands
	}
	file, err := os.Open(args[0])
	if err != nil {
		fmt.Fprintln(os.Stderr, "error: cannot read commands")
		return exitNoCommands
	}
	defer file.Close()
	out := bufio.NewWriter(os.Stdout)
	defer out.Flush()
	var set SpanSet
	scanner := bufio.NewScanner(file)
	scanner.Buffer(make([]byte, 1<<20), 1<<20)
	for number := 1; scanner.Scan(); number++ {
		if !runCommand(&set, scanner.Text(), out) {
			out.Flush()
			fmt.Fprintf(os.Stderr, "error: line %d\n", number)
			return exitBadLine
		}
	}
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
