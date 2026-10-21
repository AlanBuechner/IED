"""A stand-in for cargo and go in the language driver tests: it logs each call (its argv, folder and the environment
the drivers set) to FAKE_TOOL_LOG and does just enough of the real tool's work - cargo writes its target/ files and
prints compiler-artifact messages, go writes the -o file. FAKE_TOOL_FAIL makes it fail, FAKE_TOOL_TEST_CODE sets what
`test` exits with."""

import json
import os
import sys

ENV = ("CARGO_PROFILE_RELEASE_DEBUG", "GOOS", "GOARCH")


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8") as f:
		f.write(text)


def cargo_build(args):
	target = os.path.join(os.getcwd(), "target", "release" if "--release" in args else "debug")
	exe, pdb, rlib = (os.path.join(target, name) for name in ("game.exe", "game.pdb", "libgame.rlib"))
	for path in (exe, pdb, rlib):
		if not os.path.exists(path):   # as cargo, a build with nothing to do leaves its artifacts as they are
			write(path, "built")
	sys.stderr.write("   Compiling game v0.1.0\n")
	print(json.dumps({"reason": "compiler-artifact", "target": {"kind": ["lib"], "name": "game"}, "filenames": [rlib]}))
	print(json.dumps({"reason": "compiler-artifact", "target": {"kind": ["bin"], "name": "game"}, "filenames": [exe, pdb], "executable": exe}))
	print(json.dumps({"reason": "build-finished", "success": True}))


def main():
	tool, args = sys.argv[1], sys.argv[2:]
	with open(os.environ["FAKE_TOOL_LOG"], "a", encoding="utf-8") as f:
		f.write(json.dumps({"tool": tool, "args": args, "cwd": os.getcwd().replace("\\", "/"), "env": {k: os.environ.get(k) for k in ENV}}) + "\n")
	if os.environ.get("FAKE_TOOL_FAIL"):
		sys.stderr.write("error: it failed\n")
		return 101
	if tool == "cargo" and args[0] == "build":
		cargo_build(args)
	elif tool == "go" and args[0] == "build" and "-o" in args:
		write(args[args.index("-o") + 1], "built")
	elif args[0] == "test":
		print("test result: done")
		return int(os.environ.get("FAKE_TOOL_TEST_CODE", "0"))
	return 0


if __name__ == "__main__":
	sys.exit(main())
