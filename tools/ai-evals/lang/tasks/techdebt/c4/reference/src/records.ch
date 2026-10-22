include "grades.ch"

namespace Gradebook
{
	// A CSV file read into records, or the line that stopped it.
	struct Roster
	{
		public Core::Array<Student> students;
		public int64 badLine;                      // 0 when every line read
	}

	struct ScoreResult
	{
		public Core::Array<Core::String> unknownIds;   // in the order they were met
		public int64 badLine;
	}

	// "id,name,section" lines after a header: three fields, none empty.
	Roster ParseStudents(Core::StringView text);
	// "student_id,kind,score" lines after a header, added to the students' tallies; unknown ids are noted and skipped.
	ScoreResult ApplyScores(Core::StringView text, Core::Array<Student>& students);
}
