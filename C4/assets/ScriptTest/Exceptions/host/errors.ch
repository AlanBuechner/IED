namespace Errors
{
	class GameError
	{
	public:
		public int32 code = 0;
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int32 c) { code = c; }
		public virtual int32 Severity() const { return 1; }
	}

	int32 g_unwound = 0;

	void Fail(int32 code);
	void ThrowInt(int32 value);
	// fn() run by the host: a GameError it catches is code * 100 + severity, an int32 is its negation.
	int32 CallAndCatch(funcptr<void()> fn);
}
