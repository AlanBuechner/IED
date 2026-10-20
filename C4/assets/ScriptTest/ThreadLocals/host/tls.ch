namespace Tls
{
	thread_local int32 t_hostFrames = 0;
	int32 g_starts = 0;
	int32 g_ends = 0;
	int32 g_attempts = 0;

	int32 NextStart();
	void NoteEnd();
	int32 NextAttempt();
	void Fail(int32 value);
}
