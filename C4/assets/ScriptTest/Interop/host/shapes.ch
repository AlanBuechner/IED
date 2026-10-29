// A second file importing the header native.ch imports: the host has its bindings twice, one declaration each, and so
// must the interface its scripts compile against (it records where each file's bindings start; BUG-430).
extern "c" include "smoke.h";

int32 DoublePoint(Point p);
