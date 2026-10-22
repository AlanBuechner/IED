package main

import (
	"fmt"
	"os"
)

const (
	exitError = 1
	exitUsage = 2
)

func usage() int {
	fmt.Fprintln(os.Stderr, "usage: ledger <file> <balance|list> [--account NAME]")
	return exitUsage
}

func fail(message string) int {
	fmt.Fprintln(os.Stderr, "error: "+message)
	return exitError
}

func run(args []string) int {
	options, ok := ParseOptions(args)
	if !ok || (options.Command != "balance" && options.Command != "list") {
		return usage()
	}
	text, err := os.ReadFile(options.File)
	if err != nil {
		return fail("cannot read " + options.File)
	}
	rows, err := ParseLedger(string(text))
	if err != nil {
		return fail(err.Error())
	}
	report := Balance(rows)
	if options.Command == "list" {
		report = List(rows, options.Account)
	}
	os.Stdout.WriteString(report)
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
