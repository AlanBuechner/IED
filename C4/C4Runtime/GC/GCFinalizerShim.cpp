// The one C++ piece of the collector: a finalizer is C4 code that may throw, and an exception must not
// unwind through the collector's C frames.

#include <cstdio>
#include <cstdlib>

extern "C" void c4_gc_run_finalizer(void (*fn)(void*), void* object)
{
	try
	{
		fn(object);
	}
	catch (...)
	{
		std::fputs("C4: fatal: an exception escaped a finalizer ([operator(\"delete\")] of a heap object)\n", stderr);
		std::exit(1);
	}
}
