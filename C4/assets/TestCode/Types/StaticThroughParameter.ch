// Types - static members reached through a template parameter (BUG-172)
struct StpDouble {
	public static int32 Bonus;
	public static int32 Apply(int32 v) { return v * 2; }
}

struct StpSquare {
	public static int32 Bonus;
	public static int32 Apply(int32 v) { return v * v; }
}

void TestStaticThroughParameter();
