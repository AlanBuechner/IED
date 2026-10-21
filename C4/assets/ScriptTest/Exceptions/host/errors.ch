include "bedrock.ch"

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
	int32 g_refusalsDropped = 0;

	// An error that owns memory: the exception object is destroyed, by its host destructor, when its last handler is
	// done with it (BUG-315).
	class Refusal
	{
		public int32 code = 0;
		public Core::String message;
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int32 c, Core::StringView m) { code = c; message = Core::String(m); }
		[operator("delete")] public void Drop() { g_refusalsDropped++; }
	}

	void Fail(int32 code);
	void ThrowInt(int32 value);
	// fn() run by the host: a GameError it catches is code * 100 + severity, an int32 is its negation.
	int32 CallAndCatch(funcptr<void()> fn);
}
