// fileuris.test.js - a file outside the workspace is named by the server's form of its file: URI, whichever way Monaco
// or a language server wrote it (src/lsp/tests: CanonicalFileUri gives the same strings).

import { test, assertEqual } from "./harness.js";
import { canonicalFileUri, displayPath, fileName, isFileUri } from "/ui-kit/fileuris.js";

test("fileuris: one form for a file URI", () => {
	assertEqual(canonicalFileUri("file:///c%3A/My Lib/a.h"), "file:///C:/My%20Lib/a.h", "the server's unit test's case");
	assertEqual(canonicalFileUri("file:///C:/My%20Lib/a.h"), "file:///C:/My%20Lib/a.h", "already canonical");
	assertEqual(canonicalFileUri("file:///home/x/%C3%A9.c"), "file:///home/x/%C3%A9.c", "UTF-8 byte by byte");
	assertEqual(canonicalFileUri("file:///home/x/\u00e9.c"), "file:///home/x/%C3%A9.c");
	assertEqual(canonicalFileUri("file:///C:/a%23b%25.c"), "file:///C:/a%23b%25.c", "reserved characters stay encoded");
	assertEqual([canonicalFileUri("ied://w1/a.c"), canonicalFileUri("file:///a%2")], [null, null]);
	assertEqual(isFileUri("FILE:///x"), true);
});

test("fileuris: what a person reads", () => {
	assertEqual(displayPath("file:///C:/My%20Lib/a.h"), "C:/My Lib/a.h");
	assertEqual(displayPath("file:///home/x/a.h"), "/home/x/a.h");
	assertEqual([fileName("file:///C:/My%20Lib/my%20a.h"), fileName("src/lsp/pool.ch")], ["my a.h", "pool.ch"]);
});
