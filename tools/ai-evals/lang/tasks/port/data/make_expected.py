"""Builds the C++ mdhtml of the task's starting projects (they hold the same cpp/ folder) with the repository's clang,
runs its unit tests, and writes data/expected/<name>.html for every data/<name>.md:

	vendor\\python\\python.exe -I tools/ai-evals/lang/tasks/port/data/make_expected.py
"""

import filecmp
import glob
import os
import subprocess
import sys
import tempfile

DATA = os.path.dirname(os.path.abspath(__file__))
TASK = os.path.dirname(DATA)
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(TASK)))))
CPP = os.path.join(TASK, "c4", "fixture", "cpp")
CLANG = os.path.join(REPO, "vendor", "llvm", "bin", "clang++.exe")
LIBRARY = ["text", "lines", "inline", "lists", "document"]


def same_trees(a, b):
	compared = filecmp.dircmp(a, b)
	if compared.left_only or compared.right_only or compared.diff_files or compared.funny_files:
		return False
	return all(same_trees(os.path.join(a, d), os.path.join(b, d)) for d in compared.common_dirs)


def build(out):
	src = os.path.join(CPP, "src")
	flags = [CLANG, "-std=c++20", "-O2", "-Wall", "-Wextra"]
	program = os.path.join(out, "mdhtml.exe")
	tests = os.path.join(out, "mdhtml-tests.exe")
	subprocess.run(flags + glob.glob(os.path.join(src, "*.cpp")) + ["-o", program], check=True)
	library = [os.path.join(src, name + ".cpp") for name in LIBRARY]
	subprocess.run(flags + ["-I", src] + library + glob.glob(os.path.join(CPP, "tests", "*.cpp")) + ["-o", tests], check=True)
	subprocess.run([tests], check=True, stdout=subprocess.DEVNULL)
	return program


def main():
	if not same_trees(CPP, os.path.join(TASK, "go", "fixture", "cpp")):
		raise SystemExit("the cpp/ folders of the C4 and Go starting projects differ")
	with tempfile.TemporaryDirectory() as out:
		program = build(out)
		os.makedirs(os.path.join(DATA, "expected"), exist_ok=True)
		for source in sorted(glob.glob(os.path.join(DATA, "*.md"))):
			name = os.path.splitext(os.path.basename(source))[0]
			html = subprocess.run([program, source], check=True, capture_output=True).stdout
			with open(os.path.join(DATA, "expected", name + ".html"), "wb") as f:
				f.write(html)
			print("%s: %d bytes" % (name, len(html)))


if __name__ == "__main__":
	sys.exit(main())
