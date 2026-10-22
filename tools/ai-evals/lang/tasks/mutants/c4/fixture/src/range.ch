include "semver.ch"

namespace Semver
{
	enum class Operator { Equal, Greater, GreaterOrEqual, Less, LessOrEqual };

	// One condition of a range: how a version must compare with `version`.
	struct Comparator
	{
		public Operator op;
		public Version version;
	}

	// Comparators that must all hold; ^V and ~V each stand for two, a lower and an upper bound.
	struct Range
	{
		public Core::Array<Comparator> comparators;
	}

	// Reads comparators separated by spaces: =V, >V, >=V, <V, <=V, ^V, ~V, or a bare V meaning =V.
	// ^1.2.3 is >=1.2.3 <2.0.0, ^0.2.3 is >=0.2.3 <0.3.0, ^0.0.3 is >=0.0.3 <0.0.4; ~1.2.3 is >=1.2.3 <1.3.0.
	// False, leaving `range` as it was, for an empty range or anything else.
	bool ParseRange(Core::StringView text, Range& range);
	// Whether the version meets every comparator of the range. A version with a pre-release satisfies it only when a
	// comparator's version has the same major, minor and patch and a pre-release too (npm's rule).
	bool Satisfies(const Version& version, const Range& range);
}
