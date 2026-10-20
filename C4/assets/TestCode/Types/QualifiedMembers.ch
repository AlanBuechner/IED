// Types - members named through a qualifier of two or more parts (BUG-156)
namespace QmSpace {
	enum class Color { Red, Green, Blue }
	struct Counter { public static int32 s_count = 4; public static const int32 kLimit = 9; }
	namespace Deep { enum class Level { Low, High } }
}

void TestQualifiedMembers();
