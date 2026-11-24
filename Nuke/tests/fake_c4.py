"""A stand-in for C4.exe in Nuke's tests: records its arguments and writes the output it is asked for.
FAKE_C4_FAIL=<code> makes it fail with that exit code; FAKE_C4_WITHOUT=<option>,... plays an older compiler that
neither lists nor accepts those options; FAKE_C4_EDIT=<path> appends to that file while it compiles, as another session
editing a source during the build would. With --serve-on it is a build server (nuke/c4/server.py): each request's
build is logged with the server's process id under "server"."""

import contextlib
import io
import json
import os
import socket
import sys
import tempfile


OPTIONS = ("-o", "-O", "-g", "-D", "-l", "--projects", "--int-dir", "-test", "-test-json", "--incremental")
# Listed only with FAKE_C4_SERVE=1: the other tests compile one-shot, whatever the configuration's server setting.
SERVE_OPTIONS = ("--serve-on", "--serve-stamp", "--serve-idle")


def main(args, server=None):
	missing = [o for o in os.environ.get("FAKE_C4_WITHOUT", "").split(",") if o]
	if args == ["-h"]:
		listed = OPTIONS + (SERVE_OPTIONS if os.environ.get("FAKE_C4_SERVE") else ())
		for option in listed:
			if option not in missing:
				print(f"  {option}    an option")
		return 0
	unknown = [a for a in args if a in missing]
	if unknown:
		print(f"error: unknown option '{unknown[0]}'")
		return 2
	if "--serve-on" in args:
		return serve(args)
	log = os.environ.get("FAKE_C4_LOG")
	if log:
		with open(log, "a", encoding="utf-8") as f:
			f.write(json.dumps(args if server is None else {"server": server, "args": args}) + "\n")
	print("fake-c4: compiling")
	edited = os.environ.get("FAKE_C4_EDIT")
	if edited:
		with open(edited, "a", encoding="utf-8") as f:
			f.write("// edited while compiling\n")
	if os.environ.get("FAKE_C4_FAIL"):
		print("main.cf (1:1) error: forced failure")
		return int(os.environ["FAKE_C4_FAIL"])
	output = args[args.index("-o") + 1]
	os.makedirs(os.path.dirname(output), exist_ok=True)
	# Empty: running it fails at once (WinError 193). Text with no PE header is taken for a 16-bit program, whose modal
	# "Unsupported 16-Bit Application" dialog would hang the test that launches it.
	open(output, "w").close()
	return 0


def _option(args, name):
	return args[args.index(name) + 1]


def _without_serving(args):
	kept = []
	skip = False
	for arg in args:
		if skip:
			skip = False
		elif arg in SERVE_OPTIONS:
			skip = True
		else:
			kept.append(arg)
	return kept


def _build(args, request, stamp):
	"""The answer to one request and whether the server ends: the build's output and `done <code>`, `bye` to `quit`, or
	`stale`."""
	if request.strip() == "quit":
		return "bye\n", True
	if request.strip() != f"build {stamp}":
		return "stale\n", True
	output = io.StringIO()
	with contextlib.redirect_stdout(output):
		code = main(_without_serving(args), server=os.getpid())
	return output.getvalue() + f"done {code}\n", False


def serve(args):
	"""C4.exe --serve-on as the real one answers: a pipe (a Unix socket) of the name, one client at a time."""
	name, stamp = _option(args, "--serve-on"), _option(args, "--serve-stamp")
	if os.environ.get("FAKE_C4_SERVER_DOWN"):
		return 0
	if sys.platform.startswith("win"):
		import _winapi
		import ctypes
		kernel32 = ctypes.windll.kernel32
		first_instance = 0x00080000  # FILE_FLAG_FIRST_PIPE_INSTANCE; the byte type, byte reads and waiting are 0
		pipe = _winapi.CreateNamedPipe(rf"\\.\pipe\{name}", _winapi.PIPE_ACCESS_DUPLEX | first_instance, _winapi.PIPE_WAIT, 1, 65536, 65536, 0,
			_winapi.NULL)
		while True:
			_winapi.ConnectNamedPipe(pipe, False)
			request = b""
			while not request.endswith(b"\n"):
				chunk, _ = _winapi.ReadFile(pipe, 4096)
				request += chunk
			answer, ends = _build(args, request.decode(), stamp)
			_winapi.WriteFile(pipe, answer.encode())
			kernel32.FlushFileBuffers(ctypes.c_void_p(pipe))
			kernel32.DisconnectNamedPipe(ctypes.c_void_p(pipe))
			if ends:
				return 0
	path = os.path.join(tempfile.gettempdir(), f"{name}.sock")
	with contextlib.suppress(FileNotFoundError):
		os.unlink(path)
	listening = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	listening.bind(path)
	listening.listen(1)
	while True:
		client, _ = listening.accept()
		request = b""
		while not request.endswith(b"\n"):
			request += client.recv(4096)
		answer, ends = _build(args, request.decode(), stamp)
		client.sendall(answer.encode())
		client.close()
		if ends:
			return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
