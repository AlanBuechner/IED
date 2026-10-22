package main

import (
	"encoding/json"
	"fmt"
	"os"
)

const (
	exitFailure = 1
	exitUsage   = 2
)

func fail(format string, args ...any) int {
	fmt.Fprintf(os.Stderr, "error: "+format+"\n", args...)
	return exitFailure
}

func run(args []string) int {
	if len(args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: jsonagg <orders.json> <report.json>")
		return exitUsage
	}
	input, output := args[0], args[1]
	data, err := os.ReadFile(input)
	if err != nil {
		return fail("cannot read %s", input)
	}
	orders, err := ParseOrders(data)
	if err != nil {
		return fail("%v", err)
	}
	report, err := json.MarshalIndent(Aggregate(orders), "", "\t")
	if err != nil {
		return fail("%v", err)
	}
	if err := os.WriteFile(output, append(report, '\n'), 0o644); err != nil {
		return fail("cannot write %s", output)
	}
	return 0
}

func main() {
	os.Exit(run(os.Args[1:]))
}
