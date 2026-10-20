// Types - Scoped Enum (enum class) test declarations.
// A scoped enum keeps its members out of the enclosing scope; they are used as Color::Red.

enum class Traffic { Stop, Go, Caution };
enum class Priority : uint8 { Low = 1, High = 9 };
enum class SameNameA { X = 1 };
enum class SameNameB { X = 9 };
enum UnscopedPlain { U1 = 5, U2 = 6 };

void TestScopedEnum();
