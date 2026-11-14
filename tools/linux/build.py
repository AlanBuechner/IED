"""Runs Nuke for Linux in tools/linux/builder's container (C4 D-86): the checkout mounted at /ied, so the outputs land in
the checkout's build/bin/<config>-linux-x64 beside Windows'. C4 projects can also cross-compile on Windows (`nuke build
--system linux`); C++ projects (the compiler, its script JIT) build here, natively.

Usage: python -I tools/linux/build.py [nuke arguments...]      e.g. build C4Script -c dev -j 3
       python -I tools/linux/build.py --shell                   a shell in the container, at /ied
The image is built first when Docker has none (docker build -t ied-linux-builder tools/linux/builder)."""

import os
import shutil
import subprocess
import sys

IMAGE = "ied-linux-builder"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def docker():
	found = shutil.which("docker")
	if found:
		return found
	desktop = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
	if os.path.isfile(desktop):
		return desktop
	sys.exit("linux build: Docker is needed (Docker Desktop with its Linux engine)")


def ensure_image(program):
	if subprocess.run([program, "image", "inspect", IMAGE], capture_output=True).returncode == 0:
		return
	subprocess.run([program, "build", "-t", IMAGE, os.path.join(HERE, "builder")], check=True)


def main():
	program = docker()
	ensure_image(program)
	arguments = sys.argv[1:]
	interactive = arguments == ["--shell"]
	command = ["bash"] if interactive else ["python3", "Nuke/nuke"] + arguments + (["--system", "linux"] if "--system" not in arguments and "-s" not in arguments else [])
	mount = ROOT.replace("\\", "/") + ":/ied"
	run = [program, "run", "--rm", "--platform", "linux/amd64", "-v", mount, "-w", "/ied", "-e", "PYTHONDONTWRITEBYTECODE=1"]
	if interactive:
		run.append("-it")
	sys.exit(subprocess.run(run + [IMAGE] + command).returncode)


if __name__ == "__main__":
	main()
