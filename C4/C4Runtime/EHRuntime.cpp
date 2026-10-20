// C4 EH runtime: make an uncaught exception terminate the process cleanly instead of
// crashing. On Windows/MSVC an exception that escapes main hits std::terminate(), whose
// default is a fail-fast (STATUS_FAILFAST, 0xC0000409) - not a usable exit code. C++
// requires an uncaught throw to terminate the program with a non-zero status and never
// run code after the throw, so we install a terminate handler that does exactly that:
// print a message and exit(1). This is plain standard C++ (set_terminate + exit), so it
// behaves identically on Windows and Linux; only the compiler target flags differ.
//
// The handler is installed by a process-startup constructor, so no generated program
// needs to call anything - every linked-in exe gets clean uncaught-exception behavior. The C
// runtime keeps a terminate handler per thread, so every thread that attaches installs it too
// (ThreadRuntimeC.c).

#include <cstdio>
#include <cstdlib>
#include <exception>

// Every uncaught exception is reported here: a debugger stops on this symbol (C4DAP's "uncaught"
// filter), so it stays a call of its own.
extern "C" __attribute__((noinline)) void c4_report_uncaught_exception(const char* what)
{
	std::fputs(what, stderr);
}

static void c4_terminate_handler(void)
{
	c4_report_uncaught_exception("C4: fatal: an exception was thrown and not caught\n");
	std::exit(1);
}

// An exception reached the end of a noexcept function (the compiler wraps such a body in a catch-all
// whose handler calls this).
extern "C" void c4_noexcept_violation(void)
{
	c4_report_uncaught_exception("C4: fatal: an exception left a noexcept function\n");
	std::exit(1);
}

extern "C" void c4_install_terminate(void)
{
	std::set_terminate(c4_terminate_handler);
}

__attribute__((constructor)) static void c4_install_terminate_at_start(void)
{
	c4_install_terminate();
}
