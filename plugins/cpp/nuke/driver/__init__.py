"""The C/C++ plugin's Nuke driver (IED docs/26 section 6, Nuke/README.md section 9c): C and C++ projects built with an
LLVM release's clang for the MSVC ABI - the build system Frost was, as a language driver. A project's sources compile
with clang -cc1 (in parallel, each only when it has to), a reflecting one's also go through the Reflection library's
HeaderTool and CodeGen, and the objects link into an executable, a static library or a DLL in the project's bin
folder, with the copies that belong beside a program. It writes build/compile_commands.json for clangd.

[project] keys it reads: kind ("executable" or "library"), sources (folders, files or globs; default src), includes,
defines, links (names, paths or globs; "!NAME" leaves a file out), compiler-args (clang -cc1 arguments),
[[project.copy]], [project.config.<Name>], tests = true (a test program run by `nuke test`), depends.
[project.driver]: library = "static" | "dynamic", reflection, system-includes, linker-args (settings.py).
[languages.cpp]: llvm, system-includes, standard, c-standard. ${cpp.llvm} and ${cpp.clang} name the LLVM release."""

import os

from . import copies, database, link, model, objects, reflection, settings, toolchain as toolchains


def implicit_depends(spec):
	"""A reflecting project needs the Reflection library and its two tools built before it."""
	return [name for name in reflection.TOOLS if name != spec.name] if settings.read_options(spec).reflection else []


def variables(ctx):
	llvm = toolchains.llvm_folder(settings.read_settings(ctx.settings), ctx.workspace_dir)
	return {"llvm": llvm, "clang": f"{llvm}/bin/clang.exe"}


def program(ctx):
	return f"{ctx.project.name}.exe" if ctx.project.kind == "executable" else None


def has_tests(ctx):
	return ctx.project.kind == "executable" and ctx.project.tests is True


def test(ctx):
	"""Runs the project's test program (built first: the package asks for build-before-test) with -filter=<glob>."""
	argv = [f"{ctx.bin_dir}/{ctx.project.name}.exe"] + ([f"-filter={ctx.test_filter}"] if ctx.test_filter else []) + list(ctx.args)
	return ctx.run(argv, cwd=os.path.join(ctx.root, ctx.project.working_dir), check=False)


def build(ctx):
	try:
		return _build(ctx)
	except ValueError as error:
		ctx.problem(f"{ctx.project.file}: {error}")


def _build(ctx):
	toolchain = toolchains.resolve(settings.read_settings(ctx.settings), ctx.workspace_dir, ctx.expand)
	target = model.target(ctx)
	libraries = [model.target(dependency) for dependency in model.libraries(ctx)]
	includes = _unique(target.includes + tuple(folder for library in libraries for folder in library.includes))
	system_includes = _unique(target.system_includes + toolchain.system_includes)
	clashes = objects.shared_object_names(target.sources + ((reflection.generated_source(target),) if target.reflection else ()))
	if clashes:
		ctx.problem(f"{target.name}: " + "; ".join(clashes) + " (rename one of them)")
	os.makedirs(target.int_dir, exist_ok=True)
	os.makedirs(target.bin_dir, exist_ok=True)
	_remove_stale_objects(target, ctx.log)
	header_tool = reflection.tool_program(ctx, reflection.HEADER_TOOL) if target.reflection else None
	compiles = [objects.make_job(toolchain, ctx.configuration, target, source, includes, system_includes, header_tool) for source in target.sources]
	worked = _compile(ctx, toolchain, target, compiles, header_tool)
	database.update(ctx.workspace_dir, [database.entry(toolchain, target, job.source, includes, system_includes) for job in compiles])
	objects_linked = [job.obj for job in compiles]
	if target.reflection:
		objects_linked.append(_compile_reflection(ctx, toolchain, target, includes, system_includes, worked))
	_link(ctx, toolchain, target, objects_linked, libraries)
	if target.kind != "static":
		copied = copies.copy_all(target.copies + tuple(copy for library in libraries for copy in library.copies), target.bin_dir, ctx.log)
		if copied:
			ctx.log.line(f"{target.name}: copied {copied} file(s) beside it")
	if target.kind == "executable":
		copies.link_assets(target.dir, target.bin_dir)
	return []


def _unique(items):
	result = []
	for item in items:
		if item not in result:
			result.append(item)
	return tuple(result)


def _workers(ctx):
	return ctx.jobs or os.cpu_count() or 1


def _compile(ctx, toolchain, target, jobs, header_tool):
	"""Compiles what has to be; whether anything was. Stops Nuke when a file fails."""
	stats = objects.Stats()
	for job in jobs:
		objects.decide(job, stats, toolchain.clang, header_tool)

	def report(job):
		what = "" if job.compile_needed else " (reflection)"
		ctx.log.line(f"{target.name}: {os.path.relpath(job.source, target.dir).replace(os.sep, '/')}{what}")
		for text in job.log:
			ctx.log.line(text)
	outcome = objects.build_all(jobs, target.dir, _workers(ctx), report)
	if outcome.worked:
		reflected = f", {outcome.reflected} reflected again" if outcome.reflected else ""
		ctx.log.line(f"{target.name}: {outcome.compiled} compiled{reflected}, {outcome.failed} failed, {outcome.current} up to date")
	if outcome.failed:
		ctx.fail(f"{target.name}: compiling {outcome.failed} file(s)")
	return outcome.worked


def _compile_reflection(ctx, toolchain, target, includes, system_includes, sources_worked):
	"""Runs CodeGen when it has to and compiles generated.cpp; the object to link."""
	code_gen = reflection.tool_program(ctx, reflection.CODE_GEN)
	if reflection.needs_code_gen(target, code_gen, sources_worked):
		ctx.log.line(f"{target.name}: generating reflection")
		code = reflection.run_code_gen(target, code_gen, ctx.log)
		if code:
			ctx.fail(f"{target.name}: CodeGen", code)
	job = objects.make_job(toolchain, ctx.configuration, target, reflection.generated_source(target), includes, system_includes, None)
	_compile(ctx, toolchain, target, [job], None)
	return job.obj


def _link(ctx, toolchain, target, objects_linked, libraries):
	"""Links the target: a static library its own objects, a program or DLL those with the static libraries it reaches
	and every link of theirs and its own, less the files its own "!NAME" links leave out."""
	inputs = []
	if target.kind != "static":
		inputs += [library.output for library in libraries]
		for owner in [target] + libraries:
			inputs += model.resolve_links(owner.links, owner.dir, toolchain.library_dirs)
	left_out = model.excluded_links(target.links)
	inputs = [path for path in _unique(inputs) if os.path.basename(path).lower() not in left_out]
	code, _ = link.link(toolchain, ctx.configuration, target, objects_linked, inputs, ctx.log)
	if code:
		ctx.fail(f"{target.name}: link", code)


def _remove_stale_objects(target, log):
	"""The objects of sources removed or renamed go before anything links (the reflection's generated.obj stays)."""
	expected = {objects.object_name(source).lower() for source in target.sources} | {"generated", "linker"}
	for name in os.listdir(target.int_dir):
		stem, extension = os.path.splitext(name)
		path = os.path.join(target.int_dir, name)
		if extension in (".obj", ".d", ".args") and stem.lower() not in expected and os.path.isfile(path):
			os.remove(path)
			log.line(f"{target.name}: removed the stale {name}")
