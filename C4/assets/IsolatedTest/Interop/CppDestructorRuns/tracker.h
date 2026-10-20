// tracker.h - C++ side of CppDestructorRuns: a class whose constructors and destructor count live objects.
#pragma once
#include <cstdint>

int32_t LiveTrackers();

class Tracker {
public:
	Tracker();
	explicit Tracker(int32_t id);
	~Tracker();
	int32_t Id() const;

private:
	int32_t id;
};
