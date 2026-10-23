// The one C++ piece of the collector: finalizers and the callbacks C4 code registers are C4 code that may throw, and
// an exception must not unwind through the collector's C frames (a collection would stay half done), so one escaping
// stops the program.

#include <cstdio>
#include <cstdlib>

namespace
{
	[[noreturn]] void Escaped(const char* what)
	{
		std::fprintf(stderr, "C4: fatal: an exception escaped %s\n", what);
		std::exit(1);
	}
}

extern "C" void c4_gc_run_finalizer(void (*fn)(void*), void* object)
{
	try
	{
		fn(object);
	}
	catch (...)
	{
		Escaped("a finalizer ([operator(\"delete\")] of a heap object)");
	}
}

extern "C" void c4_gc_run_collection_result(void (*fn)(void*), void* userData)
{
	try
	{
		fn(userData);
	}
	catch (...)
	{
		Escaped("a collection-result callback (GC::RegisterCollectionResult)");
	}
}

extern "C" void c4_gc_run_root_enumerator(void (*fn)(void*, void (*)(void*, void*), void*), void* userData,
	void (*visit)(void*, void*), void* visitContext)
{
	try
	{
		fn(userData, visit, visitContext);
	}
	catch (...)
	{
		Escaped("a root enumerator (GC::RegisterRootEnumerator)");
	}
}

extern "C" bool c4_gc_run_pool_keep(bool (*fn)(void*, void*), void* userData, void* object)
{
	try
	{
		return fn(userData, object);
	}
	catch (...)
	{
		Escaped("a pool's keep callback");
	}
}

extern "C" void c4_gc_run_pool_release(void (*fn)(void*, void*), void* userData, void* object)
{
	try
	{
		fn(userData, object);
	}
	catch (...)
	{
		Escaped("a pool's released callback");
	}
}
