namespace Game
{
	int32 Add(int32 a, int32 b);
	int32 Scale(int32 v);
	int32 Twice(int32 v) { return v + v; }

	int32 g_score = 0;
	const int32 kLimit = 99;

	class Counter
	{
	public:
		public int32 count = 0;
		public static int32 s_made = 0;
		[operator("create")] public void Init() { s_made++; }
		public void Bump(int32 by);
		public int32 Get() const { return count; }
		public static int32 Made() { return s_made; }
	}
}
