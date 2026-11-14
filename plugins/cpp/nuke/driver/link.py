"""Linking a target's objects with clang's driver: lld for an executable, llvm-lib for a static library, -shared for a
DLL. A link runs when its output is missing, an object or library it takes is newer, or its arguments changed; its
arguments go through a response file, since a deep folder's objects outgrow a Windows command line."""

import glob
import os
import subprocess
import time


def response_file_command(command, path):
	"""`command` with everything after the program in a response file at `path`."""
	quoted = ['"' + arg.replace("\\", "\\\\").replace('"', '\\"') + '"' for arg in command[1:]]
	with open(path, "w", encoding="utf-8") as f:
		f.write(" ".join(quoted))
	return [command[0], "--rsp-quoting=posix", f"@{path}"]


def linux_link_command(toolchain, configuration, target, objects, libraries):
	"""A Linux target: an archive by llvm-ar; a program or a shared library by clang's driver as g++ links (libstdc++),
	with lld, finding its shared libraries beside itself ($ORIGIN). A shared library is named by its file (its soname):
	what links it by path then needs it by that name, not by the path it was built at."""
	if target.kind == "static":
		return [toolchain.archiver, "rcs", target.output] + list(objects)
	command = [toolchain.clang, "--driver-mode=g++", "--target=" + toolchain.target, "-fuse-ld=lld", "-Wl,-rpath,$ORIGIN"]
	if target.kind != "executable":
		command += ["-shared", "-fPIC", "-Wl,-soname," + os.path.basename(target.output)]
	if configuration.debug_info:
		command.append("-g")
	command += ["-o", target.output] + list(objects) + ["-Wl,--start-group"] + list(libraries) + ["-Wl,--end-group"]
	return command + list(target.linker_args) + ["-lpthread", "-ldl", "-lm"]


def link_command(toolchain, configuration, target, objects, libraries):
	if toolchain.linux:
		return linux_link_command(toolchain, configuration, target, objects, libraries)
	command = [toolchain.clang, "-target", toolchain.target]
	if target.kind == "executable":
		command.append("-fuse-ld=lld")
	elif target.kind == "static":
		command += ["-fuse-ld=llvm-lib", "-nostdlib"]   # an archive resolves nothing: no default-library flags for llvm-lib to ignore
	else:
		command += ["-shared", "-fPIC"]
	if target.kind != "static" and configuration.debug_info:
		command.append("-g")
	if target.kind != "static":
		command += toolchain.sysroot_args()
	command += ["-o", target.output] + list(objects) + list(libraries) + list(target.linker_args)
	return command


def needs_link(target, command, objects, libraries, argsfile):
	try:
		linked = os.path.getmtime(target.output)
	except OSError:
		return True
	if any(os.path.getmtime(path) > linked for path in list(objects) + list(libraries) if os.path.isfile(path)):
		return True
	try:
		with open(argsfile, encoding="utf-8") as f:
			return f.read() != " ".join(command)
	except OSError:
		return True


def move_aside_if_running(path):
	"""A running program's file cannot be written on Windows but can be renamed: it goes to <path>.<ms>.old, so the link
	can write the new one while the old keeps running. Copies left before that nothing runs any more are removed."""
	for old in glob.glob(glob.escape(path) + ".*.old"):
		try:
			os.remove(old)
		except OSError:
			pass
	if not os.path.isfile(path):
		return None
	try:
		with open(path, "r+b"):
			return None
	except PermissionError:
		aside = "%s.%d.old" % (path, int(time.time() * 1000))
		os.replace(path, aside)
		return aside


def link(toolchain, configuration, target, objects, libraries, log):
	"""Links when it has to: (exit code, whether it ran)."""
	command = link_command(toolchain, configuration, target, objects, libraries)
	argsfile = f"{target.int_dir}/linker.args"
	if not needs_link(target, command, objects, libraries, argsfile):
		return 0, False
	os.makedirs(target.bin_dir, exist_ok=True)
	if toolchain.linux and target.kind == "static" and os.path.isfile(target.output):
		os.remove(target.output)   # llvm-ar adds to an archive: the old objects would stay
	if target.kind != "static":
		aside = move_aside_if_running(target.output)
		if aside:
			log.line(f"{target.name}: {os.path.basename(target.output)} is running; moved it aside to {os.path.basename(aside)}")
	with open(argsfile, "w", encoding="utf-8") as f:
		f.write(" ".join(command))
	started = time.perf_counter()
	result = subprocess.run(response_file_command(command, f"{target.int_dir}/linker.rsp"), cwd=target.dir, stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL)
	if result.stdout.strip():
		log.write(result.stdout if result.stdout.endswith("\n") else result.stdout + "\n")
	if result.returncode != 0:
		os.remove(argsfile)
		return result.returncode, True
	log.line(f"{target.name}: linked {os.path.basename(target.output)} ({time.perf_counter() - started:.1f} s)")
	return 0, True
