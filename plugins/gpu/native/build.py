# Builds the GPU debugger's native parts (docs/22): the capture layer flashbang.dll, the replayer, the launcher, the
# sample programs and the unit tests, with the workspace toolchain's clang. Incremental: an object is rebuilt when its
# source, a header it read (clang's dependency file) or the flags changed. Run by `nuke build flashbang`.
#   python build.py --config Development --bin <out> --int <obj> --clang <clang.exe> [--only target,...] [-j N]
import argparse
import concurrent.futures
import hashlib
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
VULKAN_INCLUDE = os.path.join(REPO, "vendor", "vulkan-headers", "include")

COMMON = ["common"]

# name: kind, source folders (relative to native/), system libraries
TARGETS = {
	"flashbang": ("dll", COMMON + ["capture", "capture/d3d12", "capture/vulkan"], ["kernel32", "user32", "advapi32", "ole32", "dxgi", "dxguid"]),
	"flashbang-replay": ("exe", COMMON + ["replay", "replay/d3d12", "replay/vulkan"], ["kernel32", "user32", "d3d12", "dxgi", "dxguid", "ole32"]),
	"fb-sample-d3d12": ("exe", ["samples/d3d12"], ["kernel32", "user32", "d3d12", "dxgi", "d3dcompiler"]),
	"fb-sample-vulkan": ("exe", ["samples/vulkan"], ["kernel32", "user32"]),
	"flashbang-tests": ("exe", COMMON + ["tests"], ["kernel32", "user32", "ole32"]),
}

CONFIGS = {
	"Debug": ["-O0", "-g", "-gcodeview"],
	"Development": ["-O2", "-g", "-gcodeview"],
	"Dist": ["-O2"],
}


def log(text):
	print(text, flush=True)


def sources_of(folders):
	found = []
	for folder in folders:
		path = os.path.join(HERE, folder)
		if not os.path.isdir(path):
			continue
		for name in sorted(os.listdir(path)):
			if name.endswith(".cpp") or name.endswith(".c"):
				found.append(os.path.join(path, name))
	return found


def read_deps(dep_file):
	try:
		with open(dep_file, "r", encoding="utf-8", errors="replace") as f:
			text = f.read().replace("\\\n", " ")
	except OSError:
		return None
	_, _, rest = text.partition(":")
	deps = []
	token = ""
	i = 0
	while i < len(rest):
		c = rest[i]
		if c == "\\" and i + 1 < len(rest) and rest[i + 1] == " ":
			token += " "
			i += 2
			continue
		if c.isspace():
			if token:
				deps.append(token)
			token = ""
		else:
			token += c
		i += 1
	if token:
		deps.append(token)
	return deps


def up_to_date(obj, dep_file, stamp_file, stamp):
	if not os.path.exists(obj) or not os.path.exists(stamp_file):
		return False
	with open(stamp_file, "r", encoding="utf-8") as f:
		if f.read() != stamp:
			return False
	deps = read_deps(dep_file)
	if deps is None:
		return False
	built = os.path.getmtime(obj)
	for dep in deps:
		try:
			if os.path.getmtime(dep) > built:
				return False
		except OSError:
			return False
	return True


def compile_one(clang, source, obj, flags, defines):
	is_c = source.endswith(".c")
	args = [clang, "--target=x86_64-pc-windows-msvc", "-c", source, "-o", obj, "-MD", "-MF", obj + ".d"]
	args += ["-std=c11"] if is_c else ["-std=c++20", "-fms-extensions"]
	args += flags + defines
	args += ["-I", os.path.join(HERE, "common"), "-I", HERE, "-I", VULKAN_INCLUDE]
	args += ["-Wall", "-Wno-unused-function", "-Wno-missing-braces", "-Wno-unused-private-field", "-Wno-unknown-pragmas",
		"-Wno-microsoft-enum-forward-reference", "-Wno-nonportable-include-path", "-Wno-switch", "-Wno-unused-variable",
		"-Wno-unused-but-set-variable", "-Wno-pragma-pack", "-Wno-microsoft-cast", "-Wno-language-extension-token",
		"-fdiagnostics-absolute-paths"]
	result = subprocess.run(args, capture_output=True, text=True)
	return result.returncode, (result.stdout + result.stderr).strip()


def build_target(name, kind, folders, libs, options, pool):
	out_dir = options.bin
	int_dir = os.path.join(options.int, name)
	os.makedirs(int_dir, exist_ok=True)
	os.makedirs(out_dir, exist_ok=True)
	flags = CONFIGS[options.config]
	defines = ["-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX", "-D_CRT_SECURE_NO_WARNINGS", "-DUNICODE", "-D_UNICODE",
		"-DFB_CONFIG_" + options.config.upper(), "-DVK_USE_PLATFORM_WIN32_KHR", "-DVK_NO_PROTOTYPES"]
	stamp = json.dumps([options.clang, flags, defines])
	sources = sources_of(folders)
	jobs = []
	objects = []
	for source in sources:
		rel = os.path.relpath(source, HERE).replace("\\", "_").replace("/", "_")
		obj = os.path.join(int_dir, rel + ".obj")
		objects.append(obj)
		stamp_file = obj + ".flags"
		if up_to_date(obj, obj + ".d", stamp_file, stamp):
			continue
		jobs.append((source, obj, stamp_file))
	failed = False
	futures = {pool.submit(compile_one, options.clang, s, o, flags, defines): (s, o, f) for (s, o, f) in jobs}
	for future in concurrent.futures.as_completed(futures):
		source, obj, stamp_file = futures[future]
		code, output = future.result()
		log(f"[{name}] {os.path.relpath(source, HERE)}")
		if output:
			log(output)
		if code != 0:
			failed = True
			if os.path.exists(obj):
				os.remove(obj)
		else:
			with open(stamp_file, "w", encoding="utf-8") as f:
				f.write(stamp)
	if failed:
		return False
	output = os.path.join(out_dir, name + (".dll" if kind == "dll" else ".exe"))
	newest = max([os.path.getmtime(o) for o in objects] + [0])
	if jobs or not os.path.exists(output) or os.path.getmtime(output) < newest:
		args = [options.clang, "--target=x86_64-pc-windows-msvc", "-fuse-ld=lld"] + objects + ["-o", output]
		if kind == "dll":
			args.append("-shared")
		if options.config != "Dist":
			args.append("-g")
		args += ["-l" + lib for lib in libs]
		result = subprocess.run(args, capture_output=True, text=True)
		text = (result.stdout + result.stderr).strip()
		if text:
			log(text)
		if result.returncode != 0:
			log(f"[{name}] link failed")
			return False
		log(f"[{name}] -> {output}")
	return True


def write_layer_manifest(out_dir):
	# The Vulkan loader finds the layer through this manifest (docs/22 "Capture"); library_path is beside it.
	manifest = {
		"file_format_version": "1.2.0",
		"layer": {
			"name": "VK_LAYER_IED_flashbang",
			"type": "GLOBAL",
			"library_path": ".\\flashbang.dll",
			"api_version": "1.4.309",
			"implementation_version": "1",
			"description": "IED Flashbang frame capture",
			"functions": {
				"vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion",
			},
			"disable_environment": {"FLASHBANG_DISABLE_VULKAN": "1"},
		},
	}
	path = os.path.join(out_dir, "VkLayer_IED_flashbang.json")
	text = json.dumps(manifest, indent=2)
	if not os.path.exists(path) or open(path, "r", encoding="utf-8").read() != text:
		with open(path, "w", encoding="utf-8") as f:
			f.write(text)


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--config", default="Development", choices=sorted(CONFIGS))
	parser.add_argument("--bin", required=True)
	parser.add_argument("--int", required=True)
	parser.add_argument("--clang", required=True)
	parser.add_argument("--only", default="")
	parser.add_argument("-j", "--jobs", type=int, default=int(os.environ.get("FLASHBANG_JOBS", "3")))
	options = parser.parse_args()
	options.clang = os.path.abspath(options.clang)
	options.bin = os.path.abspath(options.bin)
	options.int = os.path.abspath(options.int)
	if not os.path.exists(options.clang):
		log(f"error: no clang at {options.clang}")
		return 1
	if not os.path.exists(os.path.join(VULKAN_INCLUDE, "vulkan", "vulkan_core.h")):
		log("error: the Vulkan headers are missing (vendor/vulkan-headers): run setup.bat")
		return 1
	names = [n for n in options.only.split(",") if n] or list(TARGETS)
	ok = True
	with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, options.jobs)) as pool:
		for name in names:
			kind, folders, libs = TARGETS[name]
			if not build_target(name, kind, folders, libs, options, pool):
				ok = False
	write_layer_manifest(options.bin)
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main())
