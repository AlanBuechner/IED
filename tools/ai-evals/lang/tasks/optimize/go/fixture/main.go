package main

import (
	"fmt"
	"os"
)

const (
	exitError = 1
	exitUsage = 2
)

func run(args []string) int {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "usage: logstats <file>")
		return exitUsage
	}
	data, err := os.ReadFile(args[0])
	if err != nil {
		fmt.Fprintln(os.Stderr, "error: cannot read "+args[0])
		return exitError
	}
	os.Stdout.WriteString(Report(Analyze(string(data))))
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
