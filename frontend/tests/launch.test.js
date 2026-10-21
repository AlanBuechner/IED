// launch.test.js - the file a launch URL names (docs/10 control `open`): "#open=<wsId>/<path>[:<line>[:<col>]]".

import { test, assertEqual } from "./harness.js";
import { parseOpenFragment } from "../src/shell/launch.js";

test("launch: the open fragment", () => {
	assertEqual(parseOpenFragment("#open=w2/docs/notes.md"), { wsId: "w2", path: "docs/notes.md", line: 0, col: 0 });
	assertEqual(parseOpenFragment("#open=w1/src/main.c:12:4"), { wsId: "w1", path: "src/main.c", line: 12, col: 4 });
	assertEqual(parseOpenFragment("open=w1/a%20b.md:3"), { wsId: "w1", path: "a b.md", line: 3, col: 0 });
	assertEqual(parseOpenFragment("#open=w2/"), { wsId: "w2", path: "", line: 0, col: 0 }, "a whole folder (`ied <folder>`, Open Folder)");
	assertEqual([parseOpenFragment(""), parseOpenFragment("#other"), parseOpenFragment("#open=w1")], [null, null, null]);
});
