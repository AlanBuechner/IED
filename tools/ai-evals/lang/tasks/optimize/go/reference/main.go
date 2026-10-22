package main

import (
	"fmt"
	"os"
)

const (
	exitError = 1
	exitUsage = 2
)

func cannotRead(path string) int {
	fmt.Fprintln(os.Stderr, "error: cannot read "+path)
	return exitError
}

func run(args []string) int {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "usage: logstats <file>")
		return exitUsage
	}
	file, err := os.Open(args[0])
	if err != nil {
		return cannotRead(args[0])
	}
	defer file.Close()
	stats, err := AnalyzeReader(file)
	if err != nil {
		return cannotRead(args[0])
	}
	os.Stdout.WriteString(Report(stats))
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
