"""A stand-in for C4.exe in Nuke's tests: records its arguments and writes the output it is asked for.
FAKE_C4_FAIL=<code> makes it fail with that exit code; FAKE_C4_WITHOUT=<option>,... plays an older compiler that
neither lists nor accepts those options; FAKE_C4_EDIT=<path> appends to that file while it compiles, as another session
editing a source during the build would."""

import json
import os
import sys


OPTIONS = ("-o", "-O", "-g", "-D", "-l", "--projects", "--int-dir", "-test", "-test-json", "--incremental")


def main(args):
	missing = [o for o in os.environ.get("FAKE_C4_WITHOUT", "").split(",") if o]
	if args == ["-h"]:
		for option in OPTIONS:
			if option not in missing:
				print(f"  {option}    an option")
		return 0
	unknown = [a for a in args if a in missing]
	if unknown:
		print(f"error: unknown option '{unknown[0]}'")
		return 2
	log = os.environ.get("FAKE_C4_LOG")
	if log:
		with open(log, "a", encoding="utf-8") as f:
			f.write(json.dumps(args) + "\n")
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
	with open(output, "w") as f:
		f.write("stub program\n")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
