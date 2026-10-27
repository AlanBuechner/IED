// paths.test.js - a relative path read from the folder of the file that names it (shared/ui-kit/paths.js): what the
// Markdown preview's links and images and an editor's links resolve to, and null for one that leaves the workspace.

import { test, assertEqual } from "./harness.js";
import { resolveRelative } from "/ui-kit/paths.js";

test("paths: a relative path from the folder that names it", () => {
	assertEqual(resolveRelative("docs/guide", "../images/a.png"), "docs/images/a.png");
	assertEqual(resolveRelative("docs", "./b.md"), "docs/b.md");
	assertEqual(resolveRelative("", "a/../c.md"), "c.md");
	assertEqual(resolveRelative("docs", "a/b/../c.md"), "docs/a/c.md");
	assertEqual(resolveRelative("docs", "/root.md"), "docs/root.md", "a leading separator is a character of the name");
	assertEqual(resolveRelative("docs", "b.md#anchor"), "docs/b.md#anchor", "an anchor travels with the path");
	assertEqual(resolveRelative("docs", "../../outside.md"), null, "above the workspace");
	assertEqual(resolveRelative("", ".."), null);
	assertEqual(resolveRelative("a", "b/../../c.md"), "c.md");
});
