"""IsolatedTest on Linux (D-86): every program under assets/IsolatedTest (or another corpus of one program a folder,
--corpus RuntimeTest) built for linux-x64 on this machine, then run together in one Linux container, each checked as
RunTests.ps1 checks it on Windows: the exit code ExpectedExit.json records ("linuxExit" where Linux's differs; 0
otherwise) and its output substrings. NegativeTests.json's cases are compile-only and the same on every system, so they
are left to RunTests.ps1; SkipTests.json's and LinuxSkipTests.json's are reported as skipped.

Usage: python -I run_linux.py [--corpus IsolatedTest] [--filter Types,Calls/Variadic] [--jobs 3] [--image ied-linux-runner] [-O2] [--keep]
Needs Docker (Docker Desktop's Linux engine) and the compiler built (build/bin/Development-windows-x64/C4/C4.exe)."""

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys

ASSETS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ASSETS)
HERE = os.path.join(ASSETS, "IsolatedTest")   # the corpus (--corpus)
COMPILER = os.path.join(os.path.dirname(REPO), "build", "bin", "Development-windows-x64", "C4", "C4.exe")
WORK = os.path.join(REPO, "assets", "build-output", "isolated-linux")   # <corpus>-linux under --corpus
SOURCES = (".cf", ".ch", ".c", ".cpp", ".h", ".hpp")
RUN_SECONDS = 120


def manifest(name):
	path = os.path.join(HERE, name)
	if not os.path.isfile(path):
		return {}
	with open(path, encoding="utf-8") as f:
		return json.load(f)


def docker():
	found = shutil.which("docker")
	if found:
		return found
	desktop = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
	if os.path.isfile(desktop):
		return desktop
	sys.exit("run_linux: Docker is needed (Docker Desktop with its Linux engine)")


def cases(filter_text):
	found = []
	for folder, _, files in os.walk(HERE):
		if folder.startswith(os.path.join(REPO, "assets", "build-output")) or not any(f.endswith(".cf") for f in files):
			continue
		rel = os.path.relpath(folder, HERE).replace("\\", "/")
		if filter_text and not any(part in rel for part in filter_text.split(",")):
			continue
		found.append(rel)
	return sorted(found)


def source_text(folder):
	text = []
	for name in sorted(os.listdir(folder)):
		if name.endswith((".cf", ".ch")):
			with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as f:
				text.append(f.read())
	return "\n".join(text)


def compile_case(rel, expected, optimize):
	"""Builds one case into WORK/rel/out; (rel, None) or (rel, why it failed)."""
	source = os.path.join(HERE, rel)
	build = os.path.join(WORK, rel)
	if os.path.isdir(build):
		shutil.rmtree(build)
	os.makedirs(build)
	for name in os.listdir(source):
		if name.endswith(SOURCES):
			shutil.copy(os.path.join(source, name), os.path.join(build, name))
	flags = ["--target", "linux-x64"]
	if optimize:
		flags.append("-O2")
	text = source_text(build)
	if re.search(r"\[test", text):
		if re.search(r"(?<![\w])main\s*\(", text):
			return rel, "has [test] functions and a main, so its tests would never run"
		flags.append("-test")
	spec = expected.get(rel, {})
	for flag in spec.get("compileFlags", []) if isinstance(spec, dict) else []:
		flags.append(flag.replace("{dir}", "/x/" + rel).replace("{case}", source))
	result = subprocess.run([COMPILER, build, "-o", os.path.join(build, "out")] + flags, capture_output=True, text=True,
		encoding="utf-8", errors="replace")
	output = result.stdout + result.stderr
	if result.returncode != 0:
		errors = [line for line in output.splitlines() if "error" in line][:3]
		return rel, "compile: " + " | ".join(errors or output.strip().splitlines()[-3:])
	return rel, None


RUNNER = r"""
cd /x
while IFS= read -r rel; do
	args=$(cat "$rel/args" 2>/dev/null)
	( cd "$rel" && eval timeout %d ./out $args > stdout.txt 2> stderr.txt; echo $? > exit.txt )
done < cases.txt
"""


def run_all(built, expected, image):
	with open(os.path.join(WORK, "cases.txt"), "w", encoding="utf-8", newline="\n") as f:
		for rel in built:
			f.write(rel + "\n")
			spec = expected.get(rel, {})
			args = spec.get("runArgs", []) if isinstance(spec, dict) else []
			with open(os.path.join(WORK, rel, "args"), "w", encoding="utf-8", newline="\n") as a:
				a.write(" ".join("'" + arg.replace("{dir}", "/x/" + rel) + "'" for arg in args))
	with open(os.path.join(WORK, "run.sh"), "w", encoding="utf-8", newline="\n") as f:
		f.write(RUNNER % RUN_SECONDS)
	mount = WORK.replace("\\", "/") + ":/x"
	subprocess.run([docker(), "run", "--rm", "--platform", "linux/amd64", "-v", mount, image, "sh", "/x/run.sh"], check=True)


def judge(rel, expected):
	build = os.path.join(WORK, rel)
	def read(name):
		path = os.path.join(build, name)
		if not os.path.isfile(path):
			return ""
		with open(path, encoding="utf-8", errors="replace") as f:
			return f.read()
	exit_text = read("exit.txt").strip()
	if not exit_text:
		return "FAIL", "did not run"
	code = int(exit_text)
	output = read("stdout.txt") + read("stderr.txt")
	spec = expected.get(rel, {})
	# "linuxExit": what the program ends with on Linux when Windows' differs (a fault is the signal's 128 + N there).
	want = spec.get("linuxExit", spec.get("exit", 0)) if isinstance(spec, dict) else 0
	if code == 124:
		return "FAIL", "timed out after %d s" % RUN_SECONDS
	if code != want:
		tail = " ".join(output.strip().splitlines()[-2:])[:200]
		return "FAIL", "exit %d, expected %d: %s" % (code, want, tail)
	for text in spec.get("mustContain", []) if isinstance(spec, dict) else []:
		if text not in output:
			return "FAIL", "output lacks '%s'" % text
	for text in spec.get("mustNotContain", []) if isinstance(spec, dict) else []:
		if text in output:
			return "FAIL", "output has '%s'" % text
	return "PASS", ""


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--filter", default="")
	parser.add_argument("--jobs", type=int, default=3)
	parser.add_argument("--corpus", default="IsolatedTest", help="a folder of assets/ holding one program a folder")
	parser.add_argument("--image", default="ied-linux-runner", help="tools/linux/runner: Debian 12 with OpenSSL")
	parser.add_argument("-O2", dest="optimize", action="store_true")
	parser.add_argument("--keep", action="store_true", help="keep the builds of passing cases")
	args = parser.parse_args()
	global HERE, WORK
	HERE = os.path.join(ASSETS, args.corpus)
	if args.corpus != "IsolatedTest":
		WORK = os.path.join(REPO, "assets", "build-output", args.corpus.lower() + "-linux")
	if not os.path.isfile(COMPILER):
		sys.exit("run_linux: build the compiler first: " + COMPILER)
	expected = manifest("ExpectedExit.json")
	negative = manifest("NegativeTests.json")
	skipped = dict(manifest("SkipTests.json"))
	skipped.update(manifest("LinuxSkipTests.json"))
	results = {}
	todo = []
	for rel in cases(args.filter):
		if rel in negative:
			continue
		if rel in skipped:
			reason = skipped[rel]
			results[rel] = ("SKIP", reason if isinstance(reason, str) else reason.get("reason", ""))
			continue
		todo.append(rel)
	os.makedirs(WORK, exist_ok=True)
	built = []
	with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
		for rel, failure in pool.map(lambda r: compile_case(r, expected, args.optimize), todo):
			if failure:
				results[rel] = ("FAIL", failure)
			else:
				built.append(rel)
	if built:
		run_all(built, expected, args.image)
	for rel in built:
		results[rel] = judge(rel, expected)
		if results[rel][0] == "PASS" and not args.keep:
			shutil.rmtree(os.path.join(WORK, rel), ignore_errors=True)
	counts = {}
	for rel in sorted(results):
		status, detail = results[rel]
		counts[status] = counts.get(status, 0) + 1
		if status != "PASS":
			print("%-4s %s  %s" % (status, rel, detail))
	print("linux-x64: " + ", ".join("%d %s" % (n, s.lower()) for s, n in sorted(counts.items())))
	sys.exit(1 if counts.get("FAIL") else 0)


if __name__ == "__main__":
	main()
