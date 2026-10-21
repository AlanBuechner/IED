package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"regexp"
	"strings"
	"time"
)

const parseTimeout = 10 * time.Minute

var viewerAddress = regexp.MustCompile(`http://\S+`)

// procView is the trace viewer's JSON for the whole trace, by P: the trace tool run headless, its viewer asked once.
func procView(goCommand, input string) (*chromeTrace, error) {
	self, err := os.Executable()
	if err != nil {
		return nil, err
	}
	tool, err := traceTool(goCommand)
	if err != nil {
		return nil, err
	}
	viewer := exec.Command(tool, "-http=127.0.0.1:0", input)
	viewer.Env = append(os.Environ(), "BROWSER="+self, openingViewer+"=1")
	log, err := viewer.StderrPipe()
	if err != nil {
		return nil, err
	}
	if err := viewer.Start(); err != nil {
		return nil, fmt.Errorf("cannot run %s: %w", tool, err)
	}
	defer func() {
		viewer.Process.Kill()
		viewer.Wait()
	}()
	address, err := listening(log)
	if err != nil {
		return nil, err
	}
	return fetch(address + "/jsontrace")
}

// listening reads the viewer's log until it says where it listens; its log is the error when it stops first.
func listening(log io.Reader) (string, error) {
	found := make(chan string, 1)
	var said strings.Builder
	go func() {
		lines := bufio.NewScanner(log)
		for lines.Scan() {
			if address := viewerAddress.FindString(lines.Text()); address != "" {
				found <- address
				io.Copy(io.Discard, log)
				return
			}
			said.WriteString(lines.Text() + "\n")
		}
		close(found)
	}()
	select {
	case address, ok := <-found:
		if !ok {
			return "", errors.New("go tool trace could not read the trace:\n" + said.String())
		}
		return address, nil
	case <-time.After(parseTimeout):
		return "", errors.New("go tool trace took too long to read the trace")
	}
}

// traceTool is the trace tool's own binary: run through `go tool`, a killed viewer would outlive its go process.
func traceTool(goCommand string) (string, error) {
	out, err := exec.Command(goCommand, "tool", "-n", "trace").Output()
	if err != nil {
		return "", fmt.Errorf("cannot find the trace tool with %s: %w", goCommand, err)
	}
	return strings.TrimSpace(string(out)), nil
}

func fetch(url string) (*chromeTrace, error) {
	response, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("the trace viewer answered %s", response.Status)
	}
	var view chromeTrace
	if err := json.NewDecoder(response.Body).Decode(&view); err != nil {
		return nil, fmt.Errorf("the trace viewer's JSON: %w", err)
	}
	return &view, nil
}
