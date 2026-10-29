"""test: compile each test-bearing C4 project's [test] harness (C4.exe -test) and run it; a project in another language
tests through its driver (`cargo test`, `go test`), custom projects through their "test" steps. Every harness runs even
when an earlier one fails; the exit code is 1 if any failed."""

from .. import engine, fsutil, process
from ..c4 import compile as c4compile
from ..c4 import ide_bridge
from . import base
from .build import copy_files


class Test(base.Command):
	name = "test"
	description = "Build and run the tests (named projects, default: every project with tests); --filter GLOB picks cases"
	policy = base.TESTS

	def __init__(self, command_table):
		self.command_table = command_table

	def work(self, session, project):
		if project.is_driven:
			if session.builds_before_test(project):
				code = engine.execute(session, self.command_table, "build", [project.name])
				if code:
					session.test_results.append((project.name, code, None))
					return
			session.log.header(f"tests: {project.name}")
			code = session.driver(project).test(session.driver_context(project))
			session.test_results.append((project.name, code, None))
			return
		if not project.is_c4:
			return
		job = c4compile.plan(session, project, test=True)
		try:
			c4compile.compile_program(session, job)
		finally:
			if session.workspace.spec.ide and project.kind == "module":
				ide_bridge.write(session.workspace, project.name, job, session.toolchain(), c4compile.compiler_args(project, session.configuration))
		copy_files(session, project)    # beside the harness, as beside a program: DLLs, shaders, content its tests load
		argv = [job.output] + (["--filter", session.test_filter] if session.test_filter else []) + session.args
		session.log.header(f"tests: {project.name}")
		code = process.run(argv, session.log, cwd=fsutil.join(project.dir, project.working_dir))
		session.test_results.append((project.name, code, job.test_results))

	def finish(self, session, targets):
		results = session.test_results
		if not results:
			if not targets:
				session.log.note("no project has tests")
			return 0
		failed = [name for name, code, _ in results if code != 0]
		for name, code, report in results:
			session.log.line(f"  {'FAILED' if code else 'passed'}  {name}" + (f"  ({code} failed assertion(s))" if code > 0 else "")
				+ (f"  results: {report}" if report else ""))
		session.log.note(f"{len(results) - len(failed)} of {len(results)} test project(s) passed")
		return 1 if failed else 0

