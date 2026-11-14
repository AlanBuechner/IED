"""Makes the config server's container image folder (docs/35, docker/config-server) from a Linux build: ied/ with the
server (ied-server, ied-server.c4i, C4Script.so) and what it serves and loads - the trees an install has (the web
client, the built-in plugins, the host API's schemas, the ui-kit, codicons, the web libraries; not Nuke, nor the services
web search runs) - and the Dockerfile beside it. Build it on the NAS (tag
ied-config-server:latest), or anywhere with `docker build -t ied-config-server:latest <folder>`.

	python -I tools/config-server/image.py --bin build/bin/Development-linux-x64 [--to build/config-server-image]

Only what changed is copied, and files gone from the build or the checkout are removed. Exit code 0 when the folder is
complete.
"""

import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "install"))

import install_ied  # noqa: E402  (its trees and its mirror: an image holds what an install holds)

PROGRAM = ("ied-server", "ied-server.c4i", "C4Script.so")
# An install's trees, but Nuke and its launchers: a config server builds nothing.
TREES = tuple((tree, skip) for tree, skip in install_ied.TREES if not tree.startswith("Nuke/") and tree != "bin")
DOCKERFILE = os.path.join(REPO, "docker", "config-server", "Dockerfile")


def make(bin_root, folder):
	"""The image folder at `folder` from the build at `bin_root`; a list of what is missing (empty when complete)."""
	missing = []
	server = os.path.join(bin_root, "ied-server")
	target = os.path.join(folder, "ied")
	os.makedirs(target, exist_ok=True)
	report = {"copied": 0, "removed": 0}
	for name in PROGRAM:
		source = os.path.join(server, name)
		if not os.path.isfile(source):
			missing.append(source)
			continue
		install_ied.copy_program(source, os.path.join(target, name), report)
	for tree, skip in TREES:
		source = os.path.join(REPO, tree)
		if not os.path.isdir(source):
			missing.append(source)
			continue
		install_ied.mirror(source, os.path.join(target, tree), skip, report, anywhere=install_ied.BUILT_IN_PLACE)
	shutil.copyfile(DOCKERFILE, os.path.join(folder, "Dockerfile"))
	print(f"{folder}: {report['copied']} copied, {report['removed']} removed")
	return missing


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
	parser.add_argument("--bin", required=True, help="the Linux build's bin folder (build/bin/<config>-linux-x64)")
	parser.add_argument("--to", default=os.path.join(REPO, "build", "config-server-image"), help="the image folder")
	args = parser.parse_args(argv)
	missing = make(os.path.abspath(args.bin), os.path.abspath(args.to))
	for path in missing:
		print(f"missing: {path}", file=sys.stderr)
	return 1 if missing else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
