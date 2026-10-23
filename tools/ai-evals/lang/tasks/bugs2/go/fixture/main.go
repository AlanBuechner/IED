package main

import (
	"fmt"
	"os"
)

const (
	exitError = 1
	exitUsage = 2
)

func fail(message string) int {
	fmt.Fprintln(os.Stderr, "error: "+message)
	return exitError
}

func run(args []string) int {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "usage: invoice <orders.csv>")
		return exitUsage
	}
	text, err := os.ReadFile(args[0])
	if err != nil {
		return fail("cannot read " + args[0])
	}
	lines, err := ParseOrders(string(text))
	if err != nil {
		return fail(err.Error())
	}
	os.Stdout.WriteString(Report(GroupOrders(lines)))
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
