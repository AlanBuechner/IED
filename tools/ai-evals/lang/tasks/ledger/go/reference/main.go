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
	fmt.Fprintln(os.Stderr, "usage: ledger <file> <balance|list|report> [--account NAME] [--month YYYY-MM]")
	return exitUsage
}

func fail(message string) int {
	fmt.Fprintln(os.Stderr, "error: "+message)
	return exitError
}

func isCommand(command string) bool {
	return command == "balance" || command == "list" || command == "report"
}

func runCommand(command string, rows []Transaction) string {
	switch command {
	case "balance":
		return Balance(rows)
	case "list":
		return List(rows)
	default:
		return Report(rows)
	}
}

func run(args []string) int {
	options, ok := ParseOptions(args)
	if !ok || !isCommand(options.Command) {
		return usage()
	}
	if options.Month != "" && !IsMonth(options.Month) {
		fmt.Fprintln(os.Stderr, "error: bad month "+options.Month)
		return exitUsage
	}
	text, err := os.ReadFile(options.File)
	if err != nil {
		return fail("cannot read " + options.File)
	}
	rows, err := ParseLedger(string(text))
	if err != nil {
		return fail(err.Error())
	}
	os.Stdout.WriteString(runCommand(options.Command, Filter(rows, options.Account, options.Month)))
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
