include "grades.ch"

namespace Gradebook
{
	// Each section by name with its students by average (best first, then by name) and its mean; then the overall
	// mean.
	Core::String Report(const Core::Array<Student>& students);
}
