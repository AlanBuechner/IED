// A [test] case runs inside a catch-all (B37): an exception that leaves the case fails it, and the run goes
// on to the next case. C4 exceptions are C++ exceptions, so `catch (...)` sees every one of them.

extern "C" void c4_test_exception_left(void);

extern "C" void c4_test_run_case(void (*test)(void))
{
	try
	{
		test();
	}
	catch (...)
	{
		c4_test_exception_left();
	}
}
