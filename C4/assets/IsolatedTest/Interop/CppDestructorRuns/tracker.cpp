#include "tracker.h"

static int32_t g_live = 0;

int32_t LiveTrackers() { return g_live; }

Tracker::Tracker() : id(0) { ++g_live; }
Tracker::Tracker(int32_t i) : id(i) { ++g_live; }
Tracker::~Tracker() { --g_live; }
int32_t Tracker::Id() const { return id; }
