"""A stand-in for clang -cc1 in Frost's tests: logs each call to STUB_CLANG_LOG, writes the object and the dependency
file it is asked for, and with STUB_CLANG_EDIT set appends to its source before it finishes, as another session saving
the file during the build would."""

import os
import sys


def main(args):
	output = args[args.index("-o") + 1]
	tree = args[args.index("-dependency-file") + 1]
	source = args[args.index("-o") - 1]
	log = os.environ.get("STUB_CLANG_LOG")
	if log:
		with open(log, "a") as f:
			f.write(" ".join(args) + "\n")
	if os.environ.get("STUB_CLANG_EDIT"):
		with open(source, "a") as f:
			f.write("// edited while compiling\n")
	with open(output, "w") as f:
		f.write("stub object\n")
	with open(tree, "w") as f:
		f.write(f"{tree}: {source}\n")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
