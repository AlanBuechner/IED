"""A stand-in for C4.exe in Nuke's tests: records its arguments and writes the output it is asked for.
FAKE_C4_FAIL=<code> makes it fail with that exit code."""

import json
import os
import sys


def main(args):
	log = os.environ.get("FAKE_C4_LOG")
	if log:
		with open(log, "a", encoding="utf-8") as f:
			f.write(json.dumps(args) + "\n")
	print("fake-c4: compiling")
	if os.environ.get("FAKE_C4_FAIL"):
		print("main.cf (1:1) error: forced failure")
		return int(os.environ["FAKE_C4_FAIL"])
	output = args[args.index("-o") + 1]
	os.makedirs(os.path.dirname(output), exist_ok=True)
	with open(output, "w") as f:
		f.write("stub program\n")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
