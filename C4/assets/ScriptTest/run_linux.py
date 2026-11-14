"""ScriptTest on Linux (D-86): RunTests.ps1's cases, each host built with --script-host by the compiler built for Linux
and run with its scripts, in tools/linux/builder's container (the compiler, its script host C4Script.so and the C4
runtime's sources are the Linux build's). Run on Windows it starts the container and runs itself in it; --inside runs
the cases where it is. LinuxSkipTests.json's cases are reported as skipped.

Usage: python -I run_linux.py [--filter Reload,GC] [-O2] [--debug-scripts] [--volume ied-linux-port-build]
       (--volume: a Docker volume holding the Linux build at /ied/build instead of the checkout's build/)"""

import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))     # C4/
ROOT = os.path.dirname(REPO)                     # the checkout
IMAGE = "ied-linux-builder"
SOURCES = (".cf", ".ch", ".c", ".h")
RUN_SECONDS = 180


def docker():
	found = shutil.which("docker")
	if found:
		return found
	desktop = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
	if os.path.isfile(desktop):
		return desktop
	sys.exit("run_linux: Docker is needed (Docker Desktop with its Linux engine)")


def skipped():
	path = os.path.join(HERE, "LinuxSkipTests.json")
	if not os.path.isfile(path):
		return {}
	with open(path, encoding="utf-8") as f:
		return json.load(f)


def copy_sources(source, target):
	os.makedirs(target, exist_ok=True)
	for name in os.listdir(source):
		if os.path.splitext(name)[1] in SOURCES:
			shutil.copyfile(os.path.join(source, name), os.path.join(target, name))


def run_case(case, compiler, work, optimize, debug):
	"""(passed, lines) of one case."""
	sub = os.path.join(work, case)
	shutil.rmtree(sub, ignore_errors=True)
	host = os.path.join(sub, "host")
	out = os.path.join(sub, "out")
	copy_sources(os.path.join(HERE, case, "host"), host)
	copy_sources(os.path.join(HERE, "Common"), host)
	os.makedirs(out)
	scripts = os.path.join(HERE, case, "scripts")
	if os.path.isdir(scripts):
		shutil.copytree(scripts, os.path.join(out, "scripts"))
	program = os.path.join(out, "host")
	arguments = [compiler, host, "-o", program, "--script-host"]
	if optimize:
		arguments.append("-O2")
	if debug:
		arguments.append("-g")
	flags = os.path.join(HERE, case, "flags.txt")
	if os.path.isfile(flags):
		with open(flags, encoding="utf-8") as f:
			arguments += f.read().split()
	built = subprocess.run(arguments, capture_output=True, text=True, errors="replace")
	if built.returncode != 0:
		return False, ["FAIL (build)  " + case] + (built.stdout + built.stderr).splitlines()[:30]
	try:
		ran = subprocess.run([program, "O2" if optimize else "O0"], cwd=out, capture_output=True, text=True, errors="replace", timeout=RUN_SECONDS)
	except subprocess.TimeoutExpired as expired:
		text = (expired.stdout or b"").decode(errors="replace") if isinstance(expired.stdout, bytes) else (expired.stdout or "")
		return False, ["FAIL (timeout %ds)  %s" % (RUN_SECONDS, case)] + text.splitlines()[-30:]
	if ran.returncode != 0:
		return False, ["FAIL (exit %d)  %s" % (ran.returncode, case)] + (ran.stdout + ran.stderr).splitlines()[-40:]
	return True, ["PASS  " + case]


def inside(args):
	compiler = os.environ.get("C4_LINUX_COMPILER") or os.path.join(ROOT, "build", "bin", "Development-linux-x64", "C4", "C4")
	if not os.path.isfile(compiler):
		print("COMPILER NOT FOUND: %s - build it first (tools/linux/build.py build C4 C4Script)" % compiler)
		return 1
	variant = ("O2" if args.O2 else "O0") + ("-debug" if args.debug_scripts else "")
	work = os.path.join(REPO, "assets", "build-output", "script-linux", variant)
	skip = skipped()
	filters = [f for f in (args.filter or "").split(",") if f]
	cases = sorted(name for name in os.listdir(HERE) if os.path.isdir(os.path.join(HERE, name, "host")))
	if filters:
		cases = [c for c in cases if any(f in c for f in filters)]
	passed = failed = skips = 0
	for case in cases:
		if case in skip:
			print("SKIP  %s (%s)" % (case, skip[case]), flush=True)
			skips += 1
			continue
		ok, lines = run_case(case, compiler, work, args.O2, args.debug_scripts)
		print(lines[0], flush=True)
		for line in lines[1:]:
			print("      " + line, flush=True)
		passed += ok
		failed += not ok
	print("\nPASSED: %d   FAILED: %d   SKIPPED: %d   TOTAL: %d" % (passed, failed, skips, passed + failed + skips))
	return 1 if failed else 0


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
	parser.add_argument("--filter", default="")
	parser.add_argument("-O2", action="store_true", dest="O2")
	parser.add_argument("--debug-scripts", action="store_true")
	parser.add_argument("--volume", default="", help="a Docker volume holding the Linux build, mounted at <checkout>/build")
	parser.add_argument("--inside", action="store_true", help="run the cases here (in the container)")
	args = parser.parse_args(argv)
	if args.inside or sys.platform.startswith("linux"):
		return inside(args)
	program = docker()
	run = [program, "run", "--rm", "--platform", "linux/amd64", "-v", ROOT.replace("\\", "/") + ":/ied", "-w", "/ied",
		"-e", "PYTHONDONTWRITEBYTECODE=1"]
	if args.volume:
		run += ["-v", args.volume + ":/ied/build"]
	inner = ["python3", "-I", "C4/assets/ScriptTest/run_linux.py", "--inside"] + [a for a in argv if a != "--inside"]
	if args.volume:
		inner = [a for a in inner if a != args.volume and a != "--volume"]
	return subprocess.run(run + [IMAGE] + inner).returncode


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
