// desktop.test.js - the desktop app's UI module (plugins/desktop, docs/27): a new window's URL.

import { test, assertEqual } from "./harness.js";
import { workspaceUrl } from "/plugins/ied.desktop/ui/index.js";

test("desktop: a new window opens on this window's workspace", () => {
	assertEqual(workspaceUrl("http://127.0.0.1:8765", "w2"), "http://127.0.0.1:8765/#open=w2/");
	assertEqual(workspaceUrl("http://127.0.0.1:8765", null), "http://127.0.0.1:8765/", "IED's home without one");
});
