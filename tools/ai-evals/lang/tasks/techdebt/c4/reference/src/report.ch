include "grades.ch"

namespace Gradebook
{
	// Each section by name with its students by average (best first, then by name), its mean and median; then the
	// overall mean and the honor roll.
	Core::String Report(const Core::Array<Student>& students);
	// The median of averages sorted best first: the middle one, or the two middle ones' mean rounded half up.
	int64 MedianOfRanked(const Core::Array<int64>& ranked);
}
