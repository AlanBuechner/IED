// browser.test.js - the Browser tab's pure parts (plugins/browser, docs/27): where a page's picture sits in the pane, a
// pointer in page pixels, the modifier bits, what an address typed becomes, and a workspace file as a file: URL.

import { test, assertEqual } from "./harness.js";
import { fitted, toPage, modifiersOf, typedUrl } from "/plugins/ied.browser/ui/browserview.js";
import { fileUrl, newPageNotice } from "/plugins/ied.browser/ui/index.js";

test("browser: a picture fits the pane, centered, its aspect kept", () => {
	assertEqual(fitted(1280, 800, 640, 400), { left: 0, top: 0, width: 640, height: 400 }, "the same aspect fills it");
	assertEqual(fitted(400, 800, 800, 400), { left: 300, top: 0, width: 200, height: 400 }, "a tall page is pillarboxed");
	assertEqual(fitted(1600, 400, 800, 400), { left: 0, top: 100, width: 800, height: 200 }, "a wide page is letterboxed");
	assertEqual(fitted(0, 0, 800, 400), { left: 0, top: 0, width: 800, height: 400 }, "nothing to fit yet");
});

test("browser: a point of the pane in page pixels", () => {
	const shown = { left: 300, top: 0, width: 200, height: 400 };
	assertEqual(toPage(300, 0, shown, { width: 400, height: 800 }), { x: 0, y: 0 }, "the picture's corner");
	assertEqual(toPage(400, 200, shown, { width: 400, height: 800 }), { x: 200, y: 400 }, "its middle, scaled");
	assertEqual(toPage(100, 200, shown, { width: 400, height: 800 }), null, "beside the picture");
});

test("browser: the modifier bits the DevTools protocol wants", () => {
	assertEqual(modifiersOf({ altKey: true }), 1);
	assertEqual(modifiersOf({ ctrlKey: true, shiftKey: true }), 10);
	assertEqual(modifiersOf({ metaKey: true }), 4);
	assertEqual(modifiersOf({}), 0);
});

test("browser: what a typed address opens", () => {
	assertEqual(typedUrl("localhost:5173"), "http://localhost:5173", "a local dev server");
	assertEqual(typedUrl("127.0.0.1:8080/app"), "http://127.0.0.1:8080/app");
	assertEqual(typedUrl("example.com"), "https://example.com", "the web");
	assertEqual(typedUrl("  https://example.com/a  "), "https://example.com/a", "as written");
	assertEqual(typedUrl("file:///C:/site/index.html"), "file:///C:/site/index.html");
	assertEqual(typedUrl(""), "about:blank");
});

test("browser: which new pages are told, and how", () => {
	assertEqual(newPageNotice({ opener: "ai", url: "http://localhost:5173/" }), "The AI opened http://localhost:5173/");
	assertEqual(newPageNotice({ opener: "user", parent: "pg-1", url: "https://example.com/" }), "A page opened https://example.com/", "a link's new page");
	assertEqual(newPageNotice({ opener: "user", parent: "" }), null, "a person's own page is in their tab already");
});

test("browser: a workspace file as a file: URL", () => {
	assertEqual(fileUrl("C:\\Users\\me\\site", "index.html"), "file:///C:/Users/me/site/index.html");
	assertEqual(fileUrl("C:/site/", "/pages/a b#1.html"), "file:///C:/site/pages/a%20b%231.html", "spaces and a hash escaped");
});
