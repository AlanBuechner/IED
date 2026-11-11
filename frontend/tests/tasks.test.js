// tasks.test.js - the task toolbar's entries (separators, variables, tasks, and a key that names neither) and the
// status bar's elapsed time.

import { test, assertEqual, assertTrue } from "./harness.js";
import { elapsedText, toolbarEntries, TaskState } from "/plugins/ied.editor/ui/tasks.js";

test("tasks: the toolbar's entries", () => {
	const offer = {
		variables: [{ key: "config", kind: "dropdown", values: ["Debug"], value: "Debug" }],
		tasks: [{ key: "build", label: "Build", slot: "build" }],
		toolbar: ["config", "|", "build", "nope"],
	};
	assertEqual(toolbarEntries(offer).map((e) => e.kind), ["variable", "separator", "task", "unknown"]);
	assertEqual(toolbarEntries(offer)[2].task.label, "Build");
	assertEqual(toolbarEntries(null), []);
});

// A TaskState whose client answers task.cancel by ending the run and task.run by starting another.
function fakeTasks() {
	const sent = [];
	const ied = {
		toast: () => {},
		commands: { run: async () => {} },
		client: {
			request: async (method, params) => {
				sent.push([method, params]);
				if (method === "task.cancel") {
					setTimeout(() => state.event("run.ended", { runId: params.runId, status: "cancelled", label: "Run" }), 0);
					return { status: "cancelling" };
				}
				if (method === "task.run") return { runId: "r9", output: "run" };
				return {};
			},
		},
	};
	const state = new TaskState(ied);
	return { state, sent };
}

test("tasks: Stop and Restart name what runs, latest first", async () => {
	const { state, sent } = fakeTasks();
	assertEqual(state.liveLabel(), "", "nothing runs");
	state.event("run.started", { runId: "r1", project: "Game", task: "run", label: "Run", startedMs: 10 });
	assertEqual(state.liveLabel(), "Run Game");
	state.event("run.started", { runId: "r2", project: "Engine", task: "build", label: "Build", startedMs: 20 });
	assertEqual(state.liveLabel(), "2 runs");
	assertEqual(state.live().map((r) => r.runId), ["r2", "r1"]);
	assertTrue(await state.stopAll(), "Stop cancels");
	assertEqual(sent.filter(([m]) => m === "task.cancel").map(([, p]) => p.runId).sort(), ["r1", "r2"]);
	await new Promise((resolve) => setTimeout(resolve, 5));
	assertEqual(state.runs.size, 0);
	assertEqual(await state.stopAll(), false, "nothing left to stop");
});

test("tasks: Restart waits for the run's end, then runs its task again", async () => {
	const { state, sent } = fakeTasks();
	state.wsId = "w1";
	state.event("run.started", { runId: "r1", project: "Game", task: "run", label: "Run", startedMs: 10 });
	const started = await state.restart();
	assertEqual(started.runId, "r9");
	assertEqual(sent.map(([m]) => m), ["task.cancel", "task.run"]);
	assertEqual(sent[1][1], { wsId: "w1", project: "Game", task: "run" });
});

test("tasks: elapsed time", () => {
	assertEqual([elapsedText(0), elapsedText(12400), elapsedText(65000), elapsedText(3723000), elapsedText(-5)], ["0:00", "0:12", "1:05", "1:02:03", "0:00"]);
});
