// A global's, static's or thread_local's [operator("delete")] is C4 code that may throw, and at exit (or at its
// thread's end) nothing can catch it.

#include "PlatformRuntimeC.h"
#include <cstdlib>

extern "C" void c4_run_exit_destroy(void (*destroy)(void*), void* object)
{
	try
	{
		destroy(object);
	}
	catch (...)
	{
		c4_fatal_report("an exception escaped the destruction of a global, static or thread_local object");
		std::_Exit(1);
	}
}
