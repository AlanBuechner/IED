package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
)

const (
	exitError = 1
	exitUsage = 2
)

func run(args []string, stdout, stderr io.Writer) int {
	if len(args) != 1 {
		fmt.Fprintln(stderr, "usage: calc <script>")
		return exitUsage
	}
	source, err := os.ReadFile(args[0])
	if err != nil {
		fmt.Fprintln(stderr, "error: cannot read "+args[0])
		return exitError
	}
	out := bufio.NewWriter(stdout)
	err = RunScript(string(source), out)
	if flushErr := out.Flush(); err == nil {
		err = flushErr
	}
	if err != nil {
		fmt.Fprintln(stderr, "error: "+err.Error())
		return exitError
	}
	return 0
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}
