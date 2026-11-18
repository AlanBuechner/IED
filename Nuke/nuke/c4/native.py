"""What a C4 program takes from the projects in other languages it depends on (Nuke/README.md section 9c): a language
driver's optional native_library(ctx) gives a library's header folders - which the depending C4 module's `includes`
search, so `extern "c" include "Api.h"` finds them - and the library files the program links."""

from dataclasses import dataclass

from .. import graph
from ..errors import NukeError


@dataclass(frozen=True)
class NativeLibrary:
	project: str
	includes: tuple   # absolute folders
	links: tuple      # library files


def libraries(session, project, test=False):
	"""The native libraries of the projects `project` names in `depends` (and `dev_depends` for a test harness)."""
	found = []
	for name in graph.edges(project, test):
		dependency = session.workspace.find_project(name)
		if not dependency.is_driven:
			continue
		function = getattr(session.driver(dependency), "native_library", None)
		given = function(session.driver_context(dependency)) if function else None
		if given is not None:
			found.append(_read(dependency.name, given))
	return found


def _read(name, given):
	if not isinstance(given, dict) or set(given) - {"includes", "links"}:
		raise NukeError(f"{name}: its driver's native_library gave {given!r}, not {{\"includes\": [...], \"links\": [...]}}")
	return NativeLibrary(name, tuple(given.get("includes", ())), tuple(given.get("links", ())))
