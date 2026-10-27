"""Package versions and version requirements. A version is semver (`1.2.3`, `1.2.3-beta.1`); a commit after a release is
a pseudo-version, the release with build metadata (`1.2.3+4.1a2b3c4d`: four commits later), which sorts after the
release and before the next one. Requirements are Cargo's: `1.2` and `^1.2` (same major, or same minor below 1.0, or
same patch below 0.1), `~1.2.3` (same minor), `=1.2.3`, `>=1.2`, `<2`, comma-separated to combine, `*` for any.
Prerelease versions satisfy only a requirement that names a prerelease of the same version."""

import re
from dataclasses import dataclass

from ..errors import NukeError

VERSION = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?$")
PART = re.compile(r"^(\^|~|=|>=|<=|>|<)?\s*v?(\d+)(?:\.(\d+|\*|x))?(?:\.(\d+|\*|x))?(?:-([0-9A-Za-z.-]+))?$")


@dataclass(frozen=True)
class Version:
	major: int
	minor: int
	patch: int
	pre: tuple = ()        # prerelease identifiers ("beta", 1)
	build: str = ""        # a pseudo-version's "<commits>.<short commit>"

	def __str__(self):
		text = f"{self.major}.{self.minor}.{self.patch}"
		if self.pre:
			text += "-" + ".".join(str(p) for p in self.pre)
		return text + (f"+{self.build}" if self.build else "")

	@property
	def release(self):
		return Version(self.major, self.minor, self.patch, self.pre)

	@property
	def distance(self):
		"""Commits after the release (a pseudo-version's), 0 for a release."""
		return int(self.build.split(".", 1)[0]) if self.build and self.build.split(".", 1)[0].isdigit() else 0

	def key(self):
		# A prerelease sorts before its release; identifiers compare numerically when both are numbers.
		pre = tuple((0, p, "") if isinstance(p, int) else (1, 0, p) for p in self.pre)
		return (self.major, self.minor, self.patch, 0 if self.pre else 1, pre, self.distance)

	def __lt__(self, other):
		return self.key() < other.key()

	def __le__(self, other):
		return self.key() <= other.key()


def parse_version(text):
	match = VERSION.match(text.strip())
	if not match:
		return None
	major, minor, patch, pre, build = match.groups()
	return Version(int(major), int(minor), int(patch), _pre(pre), build or "")


def require_version(text, where):
	version = parse_version(text)
	if version is None:
		raise NukeError(f"{where}: '{text}' is not a version (MAJOR.MINOR.PATCH, like 1.2.0)")
	return version


def pseudo_version(release, distance, commit):
	"""The version of `commit`, `distance` commits after `release` (None: no release before it)."""
	base = release.release if release else Version(0, 0, 0)
	return base if distance == 0 and release else Version(base.major, base.minor, base.patch, base.pre, f"{distance}.{commit[:8]}")


def _pre(text):
	if not text:
		return ()
	return tuple(int(p) if p.isdigit() else p for p in text.split("."))


@dataclass(frozen=True)
class Requirement:
	text: str
	ranges: tuple          # ((op, Version), ...) all of which must hold
	allows_pre: tuple = () # releases whose prereleases it accepts (those it names)

	def __str__(self):
		return self.text

	def matches(self, version):
		release = version.release
		if release.pre and (release.major, release.minor, release.patch) not in self.allows_pre:
			return False
		return all(_holds(op, release, bound) for op, bound in self.ranges)


def _holds(op, version, bound):
	if op == ">=":
		return bound <= version
	if op == ">":
		return bound < version
	if op == "<":
		return version < bound
	if op == "<=":
		return version <= bound
	return version.key() == bound.key()


def parse_requirement(text, where):
	text = text.strip()
	if text in ("", "*"):
		return Requirement("*", ())
	ranges = []
	allows = []
	for part in text.split(","):
		match = PART.match(part.strip())
		if not match:
			raise NukeError(f"{where}: '{text}' is not a version requirement (like ^1.2, ~1.2.3, =1.2.3, >=1.2, <2 or *)")
		op, major, minor, patch, pre = match.groups()
		ranges += _expand(op or "^", int(major), _number(minor), _number(patch), _pre(pre))
		if pre:
			allows.append((int(major), _number(minor) or 0, _number(patch) or 0))
	return Requirement(text, tuple(ranges), tuple(allows))


def _number(text):
	return None if text in (None, "*", "x") else int(text)


def _expand(op, major, minor, patch, pre):
	low = Version(major, minor or 0, patch or 0, pre)
	if op == "=" and minor is not None and patch is not None:
		return [("=", low)]
	if op in (">=", ">", "<", "<="):
		if op == ">" and (minor is None or patch is None):
			return [(">=", _next(major, minor, patch))]
		if op == "<=" and (minor is None or patch is None):
			return [("<", _next(major, minor, patch))]
		return [(op, low)]
	if op == "~":
		upper = Version(major + 1, 0, 0) if minor is None else Version(major, minor + 1, 0)
		return [(">=", low), ("<", upper)]
	if op == "=" or minor is None or (op == "^" and patch is None and minor is None):
		return [(">=", low), ("<", _next(major, minor, patch))]
	# Caret: the leftmost non-zero part stays.
	if major > 0:
		upper = Version(major + 1, 0, 0)
	elif minor > 0 or patch is None:
		upper = Version(0, minor + 1, 0)
	else:
		upper = Version(0, 0, patch + 1)
	return [(">=", low), ("<", upper)]


def _next(major, minor, patch):
	"""The first version after every one a partial version names (`1` -> 2.0.0, `1.2` -> 1.3.0)."""
	if minor is None:
		return Version(major + 1, 0, 0)
	if patch is None:
		return Version(major, minor + 1, 0)
	return Version(major, minor, patch + 1)


def compatible(version):
	"""The requirement a dependent developed against `version` has: that version or later with the same major (Cargo's ^)."""
	release = version.release
	return parse_requirement(f"^{release.major}.{release.minor}.{release.patch}", "inferred")


def bump(version, part):
	"""The next release after `version`: part is "major", "minor" or "patch" (a 0.x major bump is 1.0.0)."""
	release = version.release if version else Version(0, 0, 0)
	if part == "major":
		return Version(release.major + 1, 0, 0)
	if part == "minor":
		return Version(release.major, release.minor + 1, 0)
	if release.pre:
		return Version(release.major, release.minor, release.patch)
	return Version(release.major, release.minor, release.patch + 1)
