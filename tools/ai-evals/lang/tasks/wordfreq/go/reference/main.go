package main

import (
	"fmt"
	"os"
	"strconv"
)

const (
	defaultLimit   = 10
	exitCannotRead = 1
	exitUsage      = 2
)

func usage() int {
	fmt.Fprintln(os.Stderr, "usage: wordfreq <file> [N]")
	return exitUsage
}

func run(args []string) int {
	if len(args) < 1 || len(args) > 2 {
		return usage()
	}
	limit := defaultLimit
	if len(args) == 2 {
		n, err := strconv.Atoi(args[1])
		if err != nil || n < 0 {
			return usage()
		}
		limit = n
	}
	text, err := os.ReadFile(args[0])
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: cannot read %s\n", args[0])
		return exitCannotRead
	}
	os.Stdout.WriteString(Render(TopWords(CountWords(text), limit)))
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
