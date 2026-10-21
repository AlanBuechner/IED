// harness.js - a small test runner for the browser (docs/19 "Frontend unit"): tests register with `test`, run in order,
// and the results land in window.ied_test_results for tests/e2e/run_e2e.py, and on the page for a person.

const tests = [];

export function test(name, fn) {
	tests.push({ name, fn });
}

export class AssertionError extends Error {}

function show(value) {
	try {
		return JSON.stringify(value);
	}
	catch {
		return String(value);
	}
}

export function assertEqual(actual, expected, message) {
	if (show(actual) !== show(expected)) throw new AssertionError((message ? message + ": " : "") + "expected " + show(expected) + ", got " + show(actual));
}

export function assertTrue(value, message) {
	if (!value) throw new AssertionError(message || "expected true");
}

export function assertThrows(fn, kind, message) {
	try {
		fn();
	}
	catch (e) {
		if (kind && !(e instanceof kind)) throw new AssertionError((message ? message + ": " : "") + "threw " + e.constructor.name + " (" + e.message + "), not " + kind.name);
		return e;
	}
	throw new AssertionError((message ? message + ": " : "") + "did not throw");
}

export async function run(output) {
	const results = { passed: 0, failed: 0, failures: [] };
	for (const { name, fn } of tests) {
		const row = document.createElement("div");
		try {
			await fn();
			results.passed += 1;
			row.textContent = "ok   " + name;
		}
		catch (e) {
			results.failed += 1;
			results.failures.push({ name, message: e.message, stack: e.stack });
			row.textContent = "FAIL " + name + ": " + e.message;
			row.className = "fail";
		}
		output.append(row);
	}
	window.ied_test_results = results;
	document.documentElement.dataset.done = "1";
	return results;
}
