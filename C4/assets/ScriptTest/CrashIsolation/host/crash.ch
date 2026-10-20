namespace Crash
{
	// Read and written as volatile, so no optimizer (nor the inlining of host code) sees the faults coming.
	volatile int32 g_zero = 0;
	volatile int64 g_null = 0;
	volatile int64 g_frame = 0;
}
