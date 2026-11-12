// sandbox.test.js - the Sandboxes window's pure parts (plugins/sandbox, docs/34): a sandbox's state from the snapshot,
// platform labels, a new sandbox's name, the validation's summary and run outputs kept across "changed" events.

import { test, assertEqual } from "./harness.js";
import {
	activityBadge, ago, aiRunning, appendOutput, duration, enginePlatforms, followedRun, keepOutputs, runOf, runResult, sandboxState, shortPlatform, suggestName,
	validName, validationOf, validationSummary, whoOf,
} from "/plugins/ied.sandbox/ui/model.js";

const READY = { state: "ready", platforms: [{ platform: "linux/amd64", native: true }, { platform: "linux/arm64", native: false }, { platform: "linux/riscv64", native: false }] };

test("sandbox: a state for every row", () => {
	const box = { name: "a", imageBuilt: true, container: "running", busy: "", problem: "" };
	assertEqual(sandboxState(box, READY).text, "running");
	assertEqual(sandboxState(box, READY).tone, "ok");
	assertEqual(sandboxState({ ...box, busy: "building" }, READY).text, "building...", "a job on it first");
	assertEqual(sandboxState({ ...box, busy: "building" }, READY).spin, true);
	assertEqual(sandboxState({ ...box, problem: "no os" }, READY).tone, "error", "a wrong definition before all");
	assertEqual(sandboxState({ ...box, container: "stale" }, READY).text, "definition changed");
	assertEqual(sandboxState({ ...box, imageBuilt: false, container: "absent" }, READY).text, "not built");
	assertEqual(sandboxState({ ...box, container: "stopped" }, READY).text, "stopped");
	assertEqual(sandboxState(box, { state: "stopped" }).text, "Docker stopped", "nothing is known while Docker is down");
});

test("sandbox: platforms", () => {
	assertEqual(shortPlatform("linux/arm/v7"), "arm/v7");
	assertEqual(shortPlatform("windows/amd64"), "windows/amd64");
	assertEqual(enginePlatforms(READY).native, ["linux/amd64"]);
	assertEqual(enginePlatforms(READY).emulated, ["linux/arm64", "linux/riscv64"]);
	assertEqual(enginePlatforms(null).native, []);
});

test("sandbox: a new sandbox's name", () => {
	assertEqual(suggestName("ubuntu", "24.04", "linux/amd64", true, "linux/amd64", []), "ubuntu", "the default version natively");
	assertEqual(suggestName("ubuntu", "22.04", "linux/arm64", false, "linux/amd64", []), "ubuntu-2204-arm64");
	assertEqual(suggestName("debian", "12", "linux/arm/v7", false, "linux/amd64", []), "debian-12-armv7");
	assertEqual(suggestName("arch", "latest", "linux/amd64", true, "linux/amd64", ["arch"]), "arch-2", "a taken name");
	assertEqual(suggestName("opensuse-tumbleweed", "latest", "linux/s390x", true, "linux/amd64", []), "opensuse-tumbleweed-s390x");
	assertEqual(validName("ubuntu-arm64"), true);
	assertEqual(validName("Ubuntu"), false);
	assertEqual(validName("-x"), false);
	assertEqual(validName("a".repeat(41)), false);
});

test("sandbox: the validation in a few words", () => {
	const rows = [{ sandbox: "a", result: "pass" }, { sandbox: "b", result: "fail" }, { sandbox: "c", result: "running" }, { sandbox: "d", result: "queued" }];
	assertEqual(validationSummary({ running: true, rows }), "running (2 of 4 done)");
	assertEqual(validationSummary({ running: false, rows: rows.slice(0, 2) }), "1 of 2 passed");
	assertEqual(validationSummary(null), "");
	assertEqual(validationOf({ rows }, "b").result, "fail");
	assertEqual(validationOf({ rows }, "z"), null);
	assertEqual(duration(192000), "3 m 12 s");
	assertEqual(duration(41000), "41 s");
	assertEqual(duration(0), "");
});

test("sandbox: run outputs across snapshots and events", () => {
	const outputs = new Map();
	keepOutputs(outputs, [{ id: 2, output: "two\n" }, { id: 1, output: "one\n" }], true);
	appendOutput(outputs, 2, "more\n");
	keepOutputs(outputs, [{ id: 3, sandbox: "a", running: true }, { id: 2 }], false);
	assertEqual(outputs.get(2), "two\nmore\n", "a changed event keeps what came");
	assertEqual(outputs.get(3), "", "a new run starts empty");
	assertEqual(outputs.has(1), false, "a run the server dropped goes");
	assertEqual(appendOutput(outputs, 3, "abc\ndef\nghi", 6), "ghi", "cut at a line start past the limit");
	const runs = [{ id: 5, sandbox: "a", running: false, exitCode: 2 }, { id: 4, sandbox: "a", running: true }, { id: 3, sandbox: "b", running: false, exitCode: 0 }];
	assertEqual(runOf(runs, "a").id, 4, "the running one first");
	assertEqual(runOf(runs, "b").id, 3);
	assertEqual(runOf(runs, "c"), null);
	assertEqual(runResult(runs[0]), "exit 2");
	assertEqual(runResult({ running: false, stopped: "stopped" }), "stopped");
	assertEqual(runResult({ running: false, problem: "no docker", exitCode: -1 }), "failed");
});

test("sandbox: who started a run, and what the AI is doing", () => {
	const titles = new Map([["c1", "Fix endianness"]]);
	assertEqual(whoOf({ by: "ai", chatId: "c1" }, titles), { who: "AI", ai: true, chat: "Fix endianness" });
	assertEqual(whoOf({ by: "ai", chatId: "c9" }, titles).chat, "", "a chat not known yet");
	assertEqual(whoOf({ by: "owner" }, titles).who, "You");
	assertEqual(whoOf({ by: "plugin" }, titles).who, "A plugin");
	const runs = [
		{ id: 9, sandbox: "alpine", by: "owner", running: true },
		{ id: 8, sandbox: "alpine-arm64", by: "ai", running: true },
		{ id: 7, sandbox: "alpine-s390x", by: "ai", running: true },
		{ id: 6, sandbox: "alpine", by: "ai", running: false },
	];
	assertEqual(aiRunning(runs).map((r) => r.id), [8, 7]);
	assertEqual(activityBadge(runs), { count: 2, tone: "info", title: "The AI is running 2 commands in alpine-arm64, alpine-s390x" });
	assertEqual(activityBadge([runs[0], runs[3]]), null, "no badge while the AI runs nothing");
	assertEqual(followedRun(runs, null).id, 8, "the AI's newest going run first");
	assertEqual(followedRun(runs, 6).id, 6, "a run picked stays");
	assertEqual(followedRun(runs, 99).id, 8, "a picked run gone: follow again");
	assertEqual(followedRun([runs[3]], null).id, 6, "nothing going: the newest");
	assertEqual(followedRun([], null), null);
	assertEqual(ago(1000000, 1002000), "now");
	assertEqual(ago(1000000, 1041000), "41 s ago");
	assertEqual(ago(1000000, 1000000 + 3 * 60000), "3 min ago");
});
