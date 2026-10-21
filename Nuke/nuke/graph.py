"""The project dependency graph: validation, cycles and build order."""

from .errors import NukeError


def validate(workspace):
	"""Every `depends` entry names a project, C4 programs fold only modules, and there are no cycles."""
	for project in workspace.projects.values():
		for name in project.depends:
			dependency = workspace.find_project(name)
			if dependency is None:
				workspace.project(name)  # raises with a suggestion
			if dependency.name == project.name:
				raise NukeError(f"{project.file}: project '{project.name}' depends on itself")
			if project.is_c4 and dependency.is_c4 and dependency.kind == "executable":
				raise NukeError(f"{project.file}: '{project.name}' depends on the executable '{dependency.name}'; "
					"a C4 program folds in the sources of what it depends on, so that project must be kind = \"module\"")
	cycle = find_cycle(workspace)
	if cycle:
		raise NukeError("dependency cycle: " + " -> ".join(cycle))


def find_cycle(workspace):
	WHITE, GRAY, BLACK = 0, 1, 2
	colour = {name: WHITE for name in workspace.projects}
	stack = []

	def visit(name):
		colour[name] = GRAY
		stack.append(name)
		for dependency in workspace.projects[name].depends:
			dep = workspace.find_project(dependency).name
			if colour[dep] == GRAY:
				return stack[stack.index(dep):] + [dep]
			if colour[dep] == WHITE:
				found = visit(dep)
				if found:
					return found
		stack.pop()
		colour[name] = BLACK
		return None

	for name in workspace.projects:
		if colour[name] == WHITE:
			found = visit(name)
			if found:
				return found
	return None


def build_order(workspace, roots):
	"""`roots` and everything they depend on, dependencies first; each project once."""
	order, seen = [], set()

	def visit(project):
		if project.name in seen:
			return
		seen.add(project.name)
		for name in project.depends:
			visit(workspace.find_project(name))
		order.append(project)

	for root in roots:
		visit(root)
	return order


def c4_modules(workspace, root):
	"""`root` and every C4 project its `depends` reach, nearest first (the compiler's build description order)."""
	order = [root]
	for project in order:
		for name in project.depends:
			dependency = workspace.find_project(name)
			if dependency.is_c4 and dependency not in order:
				order.append(dependency)
	return order
