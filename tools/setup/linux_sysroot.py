"""The Linux x64 sysroot C4 links Linux programs against (C4/docs D-86): glibc's headers and libraries, the kernel's
headers, GCC's startup files and C++ runtime (libgcc, libgcc_eh, libsupc++, libstdc++), taken from a Debian 11
container (glibc 2.31: a program built against it runs on Debian 11 and later, Ubuntu 20.04 and later, TrueNAS SCALE,
Steam's runtime). Docker makes it; symbolic links are followed, so the tree is plain files on Windows.

Usage: python -I tools/setup/linux_sysroot.py [--out vendor/sysroots/linux-x64] [--image debian:11]"""

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

PACKAGES = ["libc6-dev", "libgcc-10-dev", "libstdc++-10-dev", "linux-libc-dev", "libssl-dev"]
# What a link and a compile read: headers, the C library, GCC's crt files and runtime archives, the dynamic loader.
FOLDERS = ["/usr/include", "/usr/lib/x86_64-linux-gnu", "/usr/lib/gcc/x86_64-linux-gnu", "/lib/x86_64-linux-gnu", "/lib64"]
STAMP = "sysroot.txt"


def docker():
	found = shutil.which("docker")
	if found:
		return found
	desktop = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
	if os.path.isfile(desktop):
		return desktop
	sys.exit("linux_sysroot: Docker is needed (Docker Desktop with its Linux engine)")


def build(out, image):
	work = tempfile.mkdtemp(prefix="c4-sysroot-")
	try:
		script = (
			# Debian 11's LTS ended (2026-08): its packages are on archive.debian.org now.
			"set -e; export DEBIAN_FRONTEND=noninteractive; "
			"echo 'deb http://archive.debian.org/debian bullseye main' > /etc/apt/sources.list; "
			"echo 'deb http://archive.debian.org/debian-security bullseye-security main' >> /etc/apt/sources.list; "
			"apt-get -o Acquire::Check-Valid-Until=false update -qq; "
			"apt-get install -y -qq --no-install-recommends " + " ".join(PACKAGES) + " >/dev/null; "
			"dpkg-query -W -f '${Package} ${Version}\\n' " + " ".join(PACKAGES) + " libc6 > /out/" + STAMP + "; "
			"cd / && tar -chf /out/sysroot.tar --ignore-failed-read " + " ".join(f.lstrip("/") for f in FOLDERS))
		subprocess.run([docker(), "run", "--rm", "--platform", "linux/amd64", "-v", work.replace("\\", "/") + ":/out", "-w", "/", image, "sh", "-c", script], check=True)
		if os.path.isdir(out):
			shutil.rmtree(out)
		os.makedirs(out)
		with tarfile.open(os.path.join(work, "sysroot.tar")) as tar:
			for member in tar.getmembers():
				# -h stores a second name of one file (libm.so.6 and libm-2.31.so) as a hard link: extracted as a copy.
				if member.issym() or member.isdev():
					continue
				if member.islnk():
					member.linkname = member.linkname.lstrip("/")
				member.name = member.name.lstrip("/")
				if ".." in member.name.split("/"):
					continue
				tar.extract(member, out, set_attrs=False)
		shutil.copy(os.path.join(work, STAMP), os.path.join(out, STAMP))
	finally:
		shutil.rmtree(work, ignore_errors=True)


def main():
	parser = argparse.ArgumentParser()
	here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
	parser.add_argument("--out", default=os.path.join(here, "vendor", "sysroots", "linux-x64"))
	parser.add_argument("--image", default="mirror.gcr.io/library/debian:11")
	args = parser.parse_args()
	build(args.out, args.image)
	print("linux_sysroot: " + args.out)
	with open(os.path.join(args.out, STAMP), encoding="utf-8") as f:
		print(f.read().strip())


if __name__ == "__main__":
	main()
