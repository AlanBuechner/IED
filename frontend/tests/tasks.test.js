// tasks.test.js - the task toolbar's entries (separators, variables, tasks, and a key that names neither) and the
// status bar's elapsed time.

import { test, assertEqual } from "./harness.js";
import { elapsedText, toolbarEntries } from "/plugins/ied.editor/ui/tasks.js";

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

test("tasks: elapsed time", () => {
	assertEqual([elapsedText(0), elapsedText(12400), elapsedText(65000), elapsedText(3723000), elapsedText(-5)], ["0:00", "0:12", "1:05", "1:02:03", "0:00"]);
});
