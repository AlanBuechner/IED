"""Build configurations: the three built-ins plus any a workspace adds or overrides."""

from dataclasses import dataclass, field, replace

from .errors import NukeError

ARCHITECTURES = {"x64": "x64", "x86_64": "x64", "amd64": "x64"}
SYSTEMS = {"windows": "windows", "win": "windows"}


@dataclass(frozen=True)
class Configuration:
	name: str
	optimize: int
	debug_info: bool
	aliases: tuple = ()
	defines: dict = field(default_factory=dict)
	incremental: bool = False   # C4's --incremental: each object optimized alone, so an edit reoptimizes only its own


BUILT_IN = (
	Configuration("Debug", optimize=0, debug_info=True, aliases=("debug",), incremental=True),
	Configuration("Development", optimize=3, debug_info=True, aliases=("dev", "development"), incremental=True),
	Configuration("Dist", optimize=3, debug_info=False, aliases=("dist", "release", "ship")),
)
DEFAULT_CONFIGURATION = "Development"


def merge(overrides):
	"""Built-ins with the workspace's `[configurations.<Name>]` tables applied (dict name -> fields)."""
	by_name = {c.name: c for c in BUILT_IN}
	for name, fields in overrides.items():
		base = by_name.get(name, Configuration(name, optimize=3, debug_info=True))
		by_name[name] = replace(base,
			optimize=fields.get("optimize", base.optimize),
			debug_info=fields.get("debug-info", base.debug_info),
			aliases=tuple(fields.get("aliases", base.aliases)),
			defines={**base.defines, **fields.get("defines", {})},
			incremental=fields.get("incremental", base.incremental))
	return list(by_name.values())


def lookup(word, configurations):
	"""The configuration a name or alias denotes (case-insensitive), or None."""
	low = word.lower()
	for c in configurations:
		if c.name.lower() == low or low in (a.lower() for a in c.aliases):
			return c
	return None


def require(word, configurations):
	found = lookup(word, configurations)
	if found is None:
		known = ", ".join(c.name for c in configurations)
		raise NukeError(f"unknown configuration '{word}' (known: {known})")
	return found


def require_architecture(word):
	if word.lower() not in ARCHITECTURES:
		raise NukeError(f"unknown architecture '{word}' (known: x64)")
	return ARCHITECTURES[word.lower()]


def require_system(word):
	if word.lower() not in SYSTEMS:
		raise NukeError(f"unknown system '{word}' (known: windows)")
	return SYSTEMS[word.lower()]
