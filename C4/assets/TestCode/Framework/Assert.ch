// Framework - Assertion declarations and shared state (header)
void Assert(int32 condition, const char* message);
void AssertEqual(int32 expected, int32 actual);
void AssertTrue(int32 condition);
void AssertFalse(int32 condition);
void AssertFloatEquals(float expected, float actual, const char* message);
void AssertIntEquals(int32 expected, int32 actual, const char* message);
int32 GetPassed();
int32 GetFailed();
