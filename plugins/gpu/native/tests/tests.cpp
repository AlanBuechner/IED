// flashbang-tests: unit tests of the native code that needs no GPU - the serializer, the capture container, JSON,
// formats, PNG, the replay protocol's framing. `nuke test flashbang` runs it; exit code 1 when a test fails.
#include "testing.h"

#include <cstdio>

namespace fb::testing
{
	std::vector<Test>& Registry() {
		static std::vector<Test> tests;
		return tests;
	}

	Registrar::Registrar(const char* name, void (*body)(Context&)) { Registry().push_back({name, body}); }

	void Context::Check(bool condition, const char* text, const char* file, int line) {
		if (condition) return;
		++failures;
		printf("FAIL: %s:%d: %s\n", file, line, text);
	}
}

int main(int argc, char** argv) {
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int failed = 0, ran = 0;
	for (const fb::testing::Test& test : fb::testing::Registry()) {
		if (filter && !strstr(test.name, filter)) continue;
		fb::testing::Context context;
		test.body(context);
		++ran;
		if (context.failures) {
			++failed;
			printf("  %s: %d failure(s)\n", test.name, context.failures);
		}
	}
	printf("%d test(s), %d failed\n", ran, failed);
	return failed ? 1 : 0;
}
