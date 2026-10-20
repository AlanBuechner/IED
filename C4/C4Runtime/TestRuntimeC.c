// C4 unit-test runtime (Phase 5).
//
// Linked into every executable (see EnsureTestRuntimeObject in BuildUtils). The compiler's
// -test flag emits a synthetic main() that runs every [test("group.path")] function and returns
// the failed-assert count as the process exit code (matching the isolated-test convention).
//
// The assert builtins are registered in semantic analysis and declared in ModuleContext so test
// code can call them without an include. Comparisons happen in C4 itself (so custom == overloads
// and float semantics work); these helpers only record pass/fail and print diagnostics.
//
// Reporting: every failure prints the assertion's file:line (the test's when the caller passed none) and
// its message. The harness times each case (c4_test_now_ns), skips the cases `--filter <glob>`
// excludes (c4_test_configure / c4_test_selected), and -test-json <path> writes a JSON summary.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int32_t g_fail  = 0;
static int32_t g_pass  = 0;
static int32_t g_total = 0;
// Case-level accounting: g_total counts test CASES (c4_test_begin), while g_pass/g_fail count
// individual ASSERTIONS. A case "passes" when it finishes without adding any failures; the summary
// must report both levels or a 72-case/248-assertion run reads as "Passed: 248 / Total: 72".
static int32_t g_passCases = 0;
static int32_t g_caseFailsAtStart = 0;

// Current case's name and file:line, set by the harness before each test runs.
static const char* g_caseName = "";
static const char* g_caseFile = "";
static int32_t     g_caseLine = 0;

// The `--filter <glob>` of this run (`*` any run of characters, `?` one); null runs every case.
static const char* g_filter = 0;

// Every finished case, for the JSON summary.
typedef struct CaseResult { const char* name; const char* file; int32_t line; int32_t passed; int64_t nanoseconds; } CaseResult;
static CaseResult* g_cases = 0;
static int32_t g_caseCount = 0;
static int32_t g_caseCapacity = 0;

// Each run starts from nothing: a program that runs its scripts' tests runs the harness more than once.
void c4_test_configure(int32_t argc, char** argv)
{
	g_fail = g_pass = g_total = g_passCases = 0;
	g_caseCount = 0;
	g_filter = 0;
	for (int32_t i = 1; i + 1 < argc; ++i)
		if (strcmp(argv[i], "--filter") == 0) g_filter = argv[++i];
}

static int32_t globMatch(const char* pattern, const char* text)
{
	if (*pattern == '\0') return *text == '\0';
	if (*pattern == '*') return globMatch(pattern + 1, text) || (*text && globMatch(pattern, text + 1));
	if (*text && (*pattern == '?' || *pattern == *text)) return globMatch(pattern + 1, text + 1);
	return 0;
}

int32_t c4_test_selected(const char* name)
{
	return !g_filter || globMatch(g_filter, name ? name : "");
}

int64_t c4_clock_now_ns(void); // ClockRuntimeC.c

// A case's duration must not jump with the system time: the monotonic clock.
int64_t c4_test_now_ns(void)
{
	return c4_clock_now_ns();
}

// Called by the harness before each test case so output is grouped hierarchically and the
// per-case source location is known for failure reporting.
void c4_test_begin(const char* name, const char* file, int32_t line)
{
	g_total++;
	g_caseFailsAtStart = g_fail;
	g_caseName = name ? name : "";
	g_caseFile = file ? file : "";
	g_caseLine = line;
	printf("[TEST] %s\n", name ? name : "");
}

// Where a failed assertion is: its own call site when the compiler passed one (B37), else the test's.
static int32_t fail(const char* file, int32_t line, const char* msg, const char* detail)
{
	g_fail++;
	if (file && file[0])
		printf("FAIL: %s:%d: ", file, line);
	else if (g_caseFile[0])
		printf("FAIL: %s:%d: ", g_caseFile, g_caseLine);
	else
		printf("FAIL: ");
	printf("%s%s%s\n", msg ? msg : "(no message)", detail[0] ? " " : "", detail);
	return 0;
}

static int32_t pass(void) { g_pass++; return 1; }

// The assertions return 1 when they hold; otherwise they record and print the failure and return 0. The
// compiler calls the `_at` forms with the call site's file and line; the plain names are for C callers.
int32_t c4_assert_true_at(int32_t cond, const char* msg, const char* file, int32_t line)
{
	return cond ? pass() : fail(file, line, msg, "");
}

int32_t c4_assert_eq_at(int64_t expected, int64_t actual, const char* msg, const char* file, int32_t line)
{
	if (expected == actual) return pass();
	char detail[96];
	snprintf(detail, sizeof(detail), "(expected %lld, got %lld)", (long long)expected, (long long)actual);
	return fail(file, line, msg, detail);
}

// Floating-point equality within a tolerance.
int32_t c4_assert_feq_at(double expected, double actual, double tol, const char* msg, const char* file, int32_t line)
{
	double d = expected - actual;
	if (d < 0) d = -d;
	if (d <= tol) return pass();
	char detail[128];
	snprintf(detail, sizeof(detail), "(expected %g, got %g, tol %g)", expected, actual, tol);
	return fail(file, line, msg, detail);
}

static int32_t sameText(const char* a, const char* b)
{
	if (!a || !b) return a == b;
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

// C-string equality.
int32_t c4_assert_se_at(const char* expected, const char* actual, const char* msg, const char* file, int32_t line)
{
	if (sameText(expected, actual)) return pass();
	char detail[192];
	snprintf(detail, sizeof(detail), "(expected \"%s\", got \"%s\")", expected ? expected : "(null)", actual ? actual : "(null)");
	return fail(file, line, msg, detail);
}

// TestCaseRunner.cpp caught an exception that left the running case.
void c4_test_exception_left(void)
{
	g_fail++;
	printf("FAIL: %s: an exception left the test\n", g_caseName);
}

int32_t assert_true(int32_t cond, const char* msg) { return c4_assert_true_at(cond, msg, 0, 0); }
int32_t assert_eq(int64_t expected, int64_t actual, const char* msg) { return c4_assert_eq_at(expected, actual, msg, 0, 0); }
int32_t assert_feq(double expected, double actual, double tol, const char* msg) { return c4_assert_feq_at(expected, actual, tol, msg, 0, 0); }
int32_t assert_se(const char* expected, const char* actual, const char* msg) { return c4_assert_se_at(expected, actual, msg, 0, 0); }

int32_t c4_test_fail_count(void)  { return g_fail; }
int32_t c4_test_pass_count(void)  { return g_pass; }
int32_t c4_test_total_count(void) { return g_total; }

static void recordCase(const char* name, int32_t passed, int64_t nanoseconds)
{
	if (g_caseCount == g_caseCapacity)
	{
		int32_t capacity = g_caseCapacity ? g_caseCapacity * 2 : 64;
		CaseResult* grown = (CaseResult*)realloc(g_cases, sizeof(CaseResult) * (size_t)capacity);
		if (!grown) return;
		g_cases = grown;
		g_caseCapacity = capacity;
	}
	CaseResult* r = &g_cases[g_caseCount++];
	r->name = name ? name : "";
	r->file = g_caseFile;
	r->line = g_caseLine;
	r->passed = passed;
	r->nanoseconds = nanoseconds;
}

// Harness calls this once per case with its wall-clock duration.
void c4_test_case_done(const char* name, int64_t nanoseconds)
{
	const int32_t passed = g_fail == g_caseFailsAtStart;
	if (passed)
		g_passCases++;
	recordCase(name, passed, nanoseconds);
	printf("       %s %s (%.3f ms)\n", passed ? "ok" : "FAILED", name ? name : "", (double)nanoseconds / 1000000.0);
}

// A JSON string: quotes, backslashes and control characters escaped.
static void writeJsonString(FILE* f, const char* text)
{
	fputc('"', f);
	for (const unsigned char* c = (const unsigned char*)text; *c; ++c)
	{
		if (*c == '"' || *c == '\\') { fputc('\\', f); fputc(*c, f); }
		else if (*c < 0x20) fprintf(f, "\\u%04x", *c);
		else fputc(*c, f);
	}
	fputc('"', f);
}

static void writeJsonSummary(const char* path)
{
	FILE* f = fopen(path, "w");
	if (!f) { printf("could not write the test summary to %s\n", path); return; }
	fprintf(f, "{\n  \"total\": %d,\n  \"passed\": %d,\n  \"failed\": %d,\n  \"assertionsPassed\": %d,\n  \"assertionsFailed\": %d,\n  \"cases\": [",
	        g_total, g_passCases, g_total - g_passCases, g_pass, g_fail);
	for (int32_t i = 0; i < g_caseCount; ++i)
	{
		const CaseResult* r = &g_cases[i];
		fprintf(f, "%s\n    { \"name\": ", i ? "," : "");
		writeJsonString(f, r->name);
		fprintf(f, ", \"file\": ");
		writeJsonString(f, r->file);
		fprintf(f, ", \"line\": %d, \"passed\": %s, \"milliseconds\": %.3f }", r->line, r->passed ? "true" : "false",
		        (double)r->nanoseconds / 1000000.0);
	}
	fprintf(f, "%s]\n}\n", g_caseCount ? "\n  " : "");
	fclose(f);
}

void c4_test_summary(const char* jsonPath)
{
	printf("\n========================================\n");
	printf("C4 Test Results\n");
	printf("  Cases:    %d passed, %d failed (of %d)\n", g_passCases, g_total - g_passCases, g_total);
	printf("  Asserts:  %d passed, %d failed\n", g_pass, g_fail);
	printf("========================================\n");
	if (jsonPath && jsonPath[0])
		writeJsonSummary(jsonPath);
	fflush(stdout);
}
