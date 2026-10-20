// ControlFlow - Exception Handling Test declarations
struct EhError {
	int32 code;
	int32 detail;
};

void TestTryCatch();
// Regression: a single function mixing a bare `throw;` rethrow AND value `throw N;` throws across nested/
// overlapping try structures. Before the entry-block throw-buffer fix this corrupted the SEH unwind table at
// runtime (0xC0000005) because the value buffer was a funclet-local alloca, not an entry-block one.
void TestThrowRethrowMix();
