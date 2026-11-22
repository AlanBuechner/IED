"""C4Testing built with ThreadSanitizer, on Linux (docs/tools/05-test-suites.md): lowering on several threads shares the
model (docs/compiler/02, "Changes while bodies lower"), and a race there is what no other suite finds.

Every C++ project C4Testing needs is built in tools/linux/tsan's container with clang's -fsanitize=thread (added by
CCC_OVERRIDE_OPTIONS to each compile and link of Nuke's C/C++ driver), into Docker volumes of this checkout's own, so its
build/ and toolchains/ keep their Windows and Linux builds. ThreadSanitizer's reports go to files, not to the output the
tests read (a test that runs the compiler compares what it prints); a report fails the run, and each one's summary and
first frames are printed. The build's and the tests' whole output is assets/build-output/tsan/output.txt. A folder of the checkout that is a junction (a submodule linked from another checkout) is
mounted where it points, since Linux sees no Windows junction.

Usage: python -I tsan_unit.py [--filter GLOB] [--jobs N]
Needs Docker (Docker Desktop's Linux engine); the images are built when Docker has none."""

import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys

ASSETS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(ASSETS)
ROOT = os.path.dirname(REPO)
BUILDER = "ied-linux-builder"
IMAGE = "ied-linux-tsan"
REPORTS = os.path.join(ASSETS, "build-output", "tsan")
TSAN_OPTIONS = "log_path=/reports/tsan exitcode=0 halt_on_error=0 second_deadlock_stack=1 history_size=4"
# ThreadSanitizer runs code 5 to 15 times slower: every test's own time limit, this many times.
TIMEOUT_SCALE = 20
# Submodules a worktree may link from its main checkout; vendor/ is Windows' (the container has its own LLVM).
LINKED = ("plugins/cpp/libraries/yaml/yaml-cpp", "third_party/codicons")


def docker():
	found = shutil.which("docker")
	if found:
		return found
	desktop = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
	if os.path.isfile(desktop):
		return desktop
	sys.exit("tsan: Docker is needed (Docker Desktop with its Linux engine)")


def ensure_images(program):
	for image, folder in ((BUILDER, "builder"), (IMAGE, "tsan")):
		if subprocess.run([program, "image", "inspect", image], capture_output=True).returncode != 0:
			subprocess.run([program, "build", "-t", image, os.path.join(ROOT, "tools", "linux", folder)], check=True)


def mounts():
	"""The checkout, its build output's volumes, the reports' folder and every linked submodule."""
	key = hashlib.sha1(ROOT.lower().encode()).hexdigest()[:10]
	found = ["-v", ROOT.replace("\\", "/") + ":/ied", "-v", f"c4-tsan-{key}-build:/ied/build", "-v", f"c4-tsan-{key}-toolchains:/ied/toolchains",
		"-v", REPORTS.replace("\\", "/") + ":/reports"]
	for folder in LINKED:
		path = os.path.join(ROOT, *folder.split("/"))
		if os.path.isdir(path) and os.path.realpath(path) != os.path.abspath(path):
			found += ["-v", os.path.realpath(path).replace("\\", "/") + ":/ied/" + folder]
	return found


def script(filter_glob, jobs):
	tests = (f"--filter={filter_glob} " if filter_glob else "") + f"--timeout-scale={TIMEOUT_SCALE}"
	return (
		"set -e\n"
		f"CCC_OVERRIDE_OPTIONS='# +-fsanitize=thread' python3 Nuke/nuke build C4Testing -c Development --system linux -j {jobs}\n"
		"rm -f /reports/tsan.*\n"
		"cd C4\n"
		"set +e\n"
		f"TSAN_OPTIONS='{TSAN_OPTIONS}' ../build/bin/Development-linux-x64/C4Testing/C4Testing {tests}\n"
		"echo \"C4Testing exit $?\"\n")


def reports():
	"""Each report file's summary line and its first frames."""
	out = []
	for path in sorted(glob.glob(os.path.join(REPORTS, "tsan.*"))):
		with open(path, encoding="utf-8", errors="replace") as f:
			lines = f.read().splitlines()
		summaries = [line for line in lines if line.startswith("SUMMARY: ThreadSanitizer")]
		out.append((os.path.basename(path), summaries, [line for line in lines if line.strip().startswith("#")][:8]))
	return out


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--filter", default="", help="the tests to run (C4Testing's --filter); every test by default")
	parser.add_argument("--jobs", type=int, default=8)
	args = parser.parse_args()
	program = docker()
	ensure_images(program)
	os.makedirs(REPORTS, exist_ok=True)
	run = [program, "run", "--rm", "--platform", "linux/amd64", "--security-opt", "seccomp=unconfined"] + mounts() + [
		"-w", "/ied", "-e", "PYTHONDONTWRITEBYTECODE=1", IMAGE, "bash", "-c", script(args.filter, args.jobs)]
	result = subprocess.run(run, capture_output=True, text=True, encoding="utf-8", errors="replace")
	output = (result.stdout + result.stderr).splitlines()
	with open(os.path.join(REPORTS, "output.txt"), "w", encoding="utf-8") as f:
		f.write("\n".join(output) + "\n")
	print("\n".join(line for line in output if "FAIL" in line or "passed" in line or "failed" in line or "C4Testing exit" in line or "error" in line.lower())[-6000:])
	found = reports()
	for name, summaries, frames in found:
		print(f"REPORT {name}: " + (summaries[0] if summaries else "(no summary)"))
		for frame in frames:
			print("    " + frame.strip())
	exit_line = [line for line in output if line.startswith("C4Testing exit")]
	if not exit_line:
		print("\n".join(output[-40:]))
	tests_passed = bool(exit_line) and exit_line[-1].endswith(" 0")
	print(f"tsan: {len(found)} report(s), C4Testing {'passed' if tests_passed else 'FAILED'}" + ("" if exit_line else " (it did not run: the build failed)"))
	sys.exit(0 if result.returncode == 0 and tests_passed and not found else 1)


if __name__ == "__main__":
	main()
