"""The Reflection library's code generation (Reflection library docs, ReflectionSystem.md): each source of a reflecting
project goes through the HeaderTool after it compiles (objects.py), which writes what it found into
<int>/generated/; then CodeGen gathers that into generated/generated.cpp, compiled with the project. CodeGen runs when
a source compiled, when CodeGen is newer than the project's output, or when generated.cpp is newer than its object
(an interrupted build left it uncompiled)."""

import os
import subprocess

from .model import dependency

REFLECTION_LIBRARY = "Reflection"
HEADER_TOOL = "HeaderTool"
CODE_GEN = "CodeGen"
TOOLS = (REFLECTION_LIBRARY, HEADER_TOOL, CODE_GEN)


def tool_program(ctx, name):
	"""The program of the reflection tool project `name` the project depends on."""
	tool = dependency(ctx, name)
	if tool is None:
		ctx.problem(f"{ctx.project.name} reflects but does not depend on {name}")
	return f"{tool.bin_dir}/{name}.exe".replace("\\", "/")


def generated_folder(target):
	return f"{target.int_dir}/generated"


def generated_source(target):
	return f"{generated_folder(target)}/generated.cpp"


def _mtime(path):
	try:
		return os.path.getmtime(path)
	except OSError:
		return None


def needs_code_gen(target, code_gen, sources_worked):
	if sources_worked:
		return True
	output, generator = _mtime(target.output), _mtime(code_gen)
	if output is None or (generator is not None and generator > output):
		return True
	source, obj = _mtime(generated_source(target)), _mtime(f"{target.int_dir}/generated.obj")
	return source is not None and (obj is None or source > obj)


def run_code_gen(target, code_gen, log):
	"""Runs CodeGen over the HeaderTool's findings; its exit code."""
	os.makedirs(generated_folder(target), exist_ok=True)
	command = [code_gen, "--generated-folder", generated_folder(target), "--source-folder", f"{target.dir}/src"]
	result = subprocess.run(command, cwd=target.dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
		errors="replace", stdin=subprocess.DEVNULL)
	if result.stdout.strip():
		log.write(result.stdout if result.stdout.endswith("\n") else result.stdout + "\n")
	if result.returncode == 0 and not os.path.isfile(generated_source(target)):
		log.line(f"error: CodeGen wrote no {generated_source(target)}")
		return 1
	return result.returncode
