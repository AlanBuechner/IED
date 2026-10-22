// mdhtml <file.md>: the Markdown file as HTML on standard output.
package main

import (
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
		fmt.Fprintln(stderr, "usage: mdhtml <file.md>")
		return exitUsage
	}
	text, err := os.ReadFile(args[0])
	if err != nil {
		fmt.Fprintln(stderr, "error: cannot read "+args[0])
		return exitError
	}
	if _, err := io.WriteString(stdout, RenderDocument(string(text))); err != nil {
		return exitError
	}
	return 0
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}
