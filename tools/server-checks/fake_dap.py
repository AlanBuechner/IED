"""A debug adapter for the checks (docs/25 "Testing"): speaks DAP over stdio and writes every request it gets to the
JSON-lines file named by its first argument. Its "program" is the launch's `program`, a text file: the program stops
at each breakpoint's line in order (a line past the file's end is not verified; an empty line binds to the next one),
steps by a line, prints "hello from <program>" when it runs off its last breakpoint and exits with the launch's
`exitCode`. Its stack has the stopped line's frame and one in `outside`, a file the launch names outside the workspace.
A launch with "refuse" set fails; a "fake/crash" request ends the adapter at once. With "--connect PORT" (or Delve's
"--client-addr=HOST:PORT") it speaks over a socket it opens to 127.0.0.1:PORT, as an adapter of transport tcp does
(docs/25), and prints to its stdout instead. A program that is a folder (a Go package, as Delve's launch names it) is
its main.go."""

import json
import os
import socket
import sys

LOG = open(sys.argv[1], "a", encoding="utf-8")
CLIENT_ADDR = [a.split(":")[-1] for a in sys.argv if a.startswith("--client-addr=")]
if "--connect" in sys.argv or CLIENT_ADDR:
	PORT = int(CLIENT_ADDR[0]) if CLIENT_ADDR else int(sys.argv[sys.argv.index("--connect") + 1])
	CONNECTION = socket.create_connection(("127.0.0.1", PORT))
	STDIN = CONNECTION.makefile("rb")
	STDOUT = CONNECTION.makefile("wb")
	print("fake adapter connected", flush=True)
else:
	STDIN = sys.stdin.buffer
	STDOUT = sys.stdout.buffer
seq = 0
launch = {}
breakpoints = {}          # path -> [line]
lines = []
at = 0
running = False


def read_message():
	length = None
	while True:
		line = STDIN.readline()
		if not line:
			return None
		line = line.strip()
		if not line:
			break
		name, _, value = line.decode("ascii").partition(":")
		if name.lower() == "content-length":
			length = int(value.strip())
	return json.loads(STDIN.read(length).decode("utf-8"))


def send(message):
	global seq
	seq += 1
	body = json.dumps(dict(message, seq=seq)).encode("utf-8")
	STDOUT.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
	STDOUT.flush()


def respond(request, body=None, error=None):
	message = {"type": "response", "request_seq": request["seq"], "command": request["command"], "success": error is None}
	if error is not None:
		message["message"] = error
	if body is not None:
		message["body"] = body
	send(message)


def event(name, body=None):
	send({"type": "event", "event": name, "body": body or {}})


def bind(line):
	"""The 1-based line a breakpoint on `line` binds to, or None past the end."""
	while line <= len(lines) and not lines[line - 1].strip():
		line += 1
	return line if line <= len(lines) else None


def stops():
	return sorted(set(b for b in (bind(line) for line in breakpoints.get(launch.get("program"), [])) if b))


def run_on():
	"""Runs from the current line to the next breakpoint, or to the end."""
	global at, running
	running = True
	later = [line for line in stops() if line > at]
	if later:
		at = later[0]
		running = False
		event("stopped", {"reason": "breakpoint", "threadId": 1, "allThreadsStopped": True})
		return
	event("output", {"category": "stdout", "output": "hello from %s\n" % os.path.basename(launch["program"])})
	event("exited", {"exitCode": launch.get("exitCode", 0)})
	event("terminated")


def frames():
	program = launch["program"]
	stack = [{"id": 1, "name": "main", "source": {"name": os.path.basename(program), "path": program}, "line": at, "column": 1}]
	if launch.get("outside"):
		stack.append({"id": 2, "name": "caller", "source": {"name": os.path.basename(launch["outside"]), "path": launch["outside"]}, "line": 3, "column": 5})
	return stack


def handle(request):
	global launch, lines, at
	command, arguments = request["command"], request.get("arguments") or {}
	if command == "initialize":
		respond(request, {"supportsConfigurationDoneRequest": True, "supportsTerminateRequest": True, "supportsEvaluateForHovers": True,
			"exceptionBreakpointFilters": [{"filter": "uncaught", "label": "Uncaught", "default": True}, {"filter": "thrown", "label": "Thrown", "default": False}]})
		event("initialized")
	elif command == "launch":
		if arguments.get("refuse"):
			respond(request, error="the program would not start")
			return
		launch = arguments
		if os.path.isdir(launch["program"]):
			launch = dict(arguments, program=os.path.join(arguments["program"], "main.go"))
		with open(launch["program"], encoding="utf-8") as f:
			lines = f.read().split("\n")
		respond(request)
		# The adapter's own process stands for the program's (Diagnostic Tools samples it).
		event("process", {"name": os.path.basename(launch["program"]), "systemProcessId": os.getpid(), "isLocalProcess": True, "startMethod": "launch"})
	elif command == "setBreakpoints":
		path = arguments["source"]["path"]
		breakpoints[path] = [b["line"] for b in arguments.get("breakpoints", [])]
		results = []
		for line in breakpoints[path]:
			bound = bind(line) if path == launch.get("program") else line
			results.append({"id": line, "verified": bound is not None, "line": bound or line} if bound else {"id": line, "verified": False, "message": "no code on line %d" % line})
		respond(request, {"breakpoints": results})
	elif command in ("setExceptionBreakpoints", "configurationDone"):
		respond(request)
		if command == "configurationDone":
			if launch.get("stopOnEntry"):
				at = 1
				event("stopped", {"reason": "entry", "threadId": 1})
			else:
				run_on()
	elif command == "threads":
		respond(request, {"threads": [{"id": 1, "name": "main"}]})
	elif command == "stackTrace":
		respond(request, {"stackFrames": frames(), "totalFrames": len(frames())})
	elif command == "scopes":
		respond(request, {"scopes": [{"name": "Locals", "variablesReference": 100, "expensive": False}]})
	elif command == "variables":
		respond(request, {"variables": [{"name": "line", "value": str(at), "type": "int32", "variablesReference": 0}]})
	elif command == "evaluate":
		respond(request, {"result": "%s = %d" % (arguments["expression"], at), "variablesReference": 0})
	elif command in ("next", "stepIn", "stepOut"):
		respond(request)
		at += 1
		event("stopped", {"reason": "step", "threadId": 1})
	elif command == "continue":
		respond(request, {"allThreadsContinued": True})
		run_on()
	elif command == "pause":
		respond(request)
		event("stopped", {"reason": "pause", "threadId": 1})
	elif command in ("terminate", "disconnect"):
		respond(request)
		if command == "terminate":
			event("exited", {"exitCode": 1})
			event("terminated")
		else:
			sys.exit(0)
	elif command == "fake/crash":
		os._exit(3)
	else:
		respond(request, error="unknown request %s" % command)


def main():
	LOG.write(json.dumps({"argv": sys.argv[2:]}) + "\n")
	LOG.flush()
	while True:
		request = read_message()
		if request is None:
			return
		LOG.write(json.dumps(request) + "\n")
		LOG.flush()
		handle(request)


main()
