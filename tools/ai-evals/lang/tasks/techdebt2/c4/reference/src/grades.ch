include "bedrock.ch"

namespace Gradebook
{
	enum class Kind { Homework, Quiz, Exam }

	const int64 kKinds = 3;
	const int64 kMaxScore = 100;

	// Scores of one kind: their sum and how many.
	struct Tally
	{
		public int64 sum;
		public int64 count;
	}

	struct Student
	{
		public Core::String id;
		public Core::String name;
		public Core::String section;
		public Tally[kKinds] tallies;
	}

	// The average of a tally in hundredths of a point, rounded half up; 0 for no scores.
	int64 TallyAverage(const Tally& tally);
	// Homework 30%, quizzes 20%, exams 50%, in hundredths of a point.
	int64 WeightedAverage(const Student& student);
	// A for 90 and up, then B, C, D at 80, 70, 60; F below.
	const char* Letter(int64 averageHundredths);
	// An average in hundredths written with one decimal, rounded half up: 8575 is "85.8".
	void AppendTenths(Core::StringBuilder& out, int64 hundredths);
	// Half up: the mean of `count` values whose sum is `sum`.
	int64 RoundedMean(int64 sum, int64 count);
}
