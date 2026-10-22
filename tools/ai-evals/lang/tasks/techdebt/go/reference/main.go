package main

import (
	"fmt"
	"os"
)

const (
	exitError = 1
	exitUsage = 2
)

func readFile(path string) (string, bool) {
	data, err := os.ReadFile(path)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: cannot read %s\n", path)
		return "", false
	}
	return string(data), true
}

func badLine(path string, line int) int {
	fmt.Fprintf(os.Stderr, "error: %s line %d\n", path, line)
	return exitError
}

func warnUnknown(ids []string) {
	for _, id := range ids {
		fmt.Fprintf(os.Stderr, "warning: unknown student %s\n", id)
	}
}

func warnCount(count int) {
	if count > 0 {
		fmt.Fprintf(os.Stderr, "%d warning(s)\n", count)
	}
}

func run(args []string) int {
	if len(args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: gradebook <students.csv> <scores.csv>")
		return exitUsage
	}
	studentsText, ok := readFile(args[0])
	if !ok {
		return exitError
	}
	roster := ParseStudents(studentsText)
	if roster.BadLine != 0 {
		return badLine(args[0], roster.BadLine)
	}
	scoresText, ok := readFile(args[1])
	if !ok {
		return exitError
	}
	scores := ApplyScores(scoresText, roster.Students)
	warnUnknown(scores.UnknownIDs)
	if scores.BadLine != 0 {
		return badLine(args[1], scores.BadLine)
	}
	os.Stdout.WriteString(Report(roster.Students))
	warnCount(len(scores.UnknownIDs))
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
