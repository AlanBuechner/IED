namespace Debugged
{
	// Where Twice starts in scripts/debugged.cf.
	const int32 kTwiceFirstLine = 6;
	// Read as volatile, so the optimizer does not see the fault coming.
	volatile int64 g_null = 0;
}
