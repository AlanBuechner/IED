"""The C4 suites on Linux (D-86): each program built for linux-x64 by this checkout's compiler and run in
tools/linux/runner's container (Debian 12 with OpenSSL): IsolatedTest and RuntimeTest (IsolatedTest/run_linux.py),
TestCode (e2e) at -O0 and -O2, and Bedrock's tests. Exit 0 when every suite passes.

Usage: python -I linux_gates.py [--jobs 3] [--only isolated,runtime,e2e,bedrock]
Needs Docker (Docker Desktop's Linux engine) and the compiler built (build/bin/Development-windows-x64/C4/C4.exe)."""

import argparse
import os
import shutil
import subprocess
import sys

ASSETS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(ASSETS)
ROOT = os.path.dirname(REPO)
COMPILER = os.path.join(ROOT, "build", "bin", "Development-windows-x64", "C4", "C4.exe")
WORK = os.path.join(ASSETS, "build-output", "linux-gates")
IMAGE = "ied-linux-runner"
RUN_SECONDS = 1200


def docker():
	found = shutil.which("docker")
	if found:
		return found
	desktop = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
	if os.path.isfile(desktop):
		return desktop
	sys.exit("linux gates: Docker is needed (Docker Desktop with its Linux engine)")


def ensure_image(program):
	if subprocess.run([program, "image", "inspect", IMAGE], capture_output=True).returncode != 0:
		subprocess.run([program, "build", "-t", IMAGE, os.path.join(ROOT, "tools", "linux", "runner")], check=True)


def corpus(name, jobs):
	script = os.path.join(ASSETS, "IsolatedTest", "run_linux.py")
	return subprocess.run([sys.executable, "-I", script, "--corpus", name, "--jobs", str(jobs)]).returncode == 0


def build_and_run(program, label, sources, flags):
	"""One program of tests built for Linux and run in the container: whether it exited 0."""
	os.makedirs(WORK, exist_ok=True)
	output = os.path.join(WORK, label)
	built = subprocess.run([COMPILER, sources, "--target", "linux-x64", "-o", output] + flags, capture_output=True, text=True,
		encoding="utf-8", errors="replace")
	if built.returncode != 0:
		errors = [line for line in (built.stdout + built.stderr).splitlines() if "error" in line][:5]
		print(f"FAIL {label}: compile: " + " | ".join(errors))
		return False
	mount = WORK.replace("\\", "/") + ":/x"
	run = subprocess.run([program, "run", "--rm", "--platform", "linux/amd64", "-v", mount, "-w", "/x", IMAGE, "timeout", str(RUN_SECONDS),
		"./" + label], capture_output=True, text=True, encoding="utf-8", errors="replace")
	tail = (run.stdout + run.stderr).strip().splitlines()[-6:]
	print(f"{'PASS' if run.returncode == 0 else 'FAIL'} {label} (exit {run.returncode})")
	if run.returncode != 0:
		for line in tail:
			print("    " + line)
	return run.returncode == 0


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--jobs", type=int, default=3)
	parser.add_argument("--only", default="isolated,runtime,e2e,bedrock")
	args = parser.parse_args()
	if not os.path.isfile(COMPILER):
		sys.exit("linux gates: build the compiler first: " + COMPILER)
	program = docker()
	ensure_image(program)
	suites = args.only.split(",")
	results = {}
	if "isolated" in suites:
		results["isolated"] = corpus("IsolatedTest", args.jobs)
	if "runtime" in suites:
		results["runtime"] = corpus("RuntimeTest", args.jobs)
	if "e2e" in suites:
		code = os.path.join(ASSETS, "TestCode")
		results["e2e -O0"] = build_and_run(program, "e2e-O0", code, ["-O0", "-j", str(args.jobs)])
		results["e2e -O2"] = build_and_run(program, "e2e-O2", code, ["-O2", "-j", str(args.jobs)])
	if "bedrock" in suites:
		results["bedrock"] = build_and_run(program, "BedrockTests", os.path.join(REPO, "Bedrock", "Tests"), ["-test", "-j", str(args.jobs)])
	print("linux gates: " + ", ".join(f"{name} {'passed' if ok else 'FAILED'}" for name, ok in results.items()))
	sys.exit(0 if all(results.values()) else 1)


if __name__ == "__main__":
	main()
