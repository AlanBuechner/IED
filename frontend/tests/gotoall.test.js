// gotoall.test.js - Go To All's rows: the prefixes that pick a kind, workspace symbols by their ied: URI (others left
// out), and files and symbols ranked together by how well their names match, files found through their folders last.

import { test, assertEqual } from "./harness.js";
import { matchTier, parseQuery, pathOfUri, rowsOf, symbolIconName, symbolTarget } from "/plugins/ied.editor/ui/gotoall.js";

test("gotoall: a prefix picks files or symbols", () => {
	assertEqual(parseQuery(" pool "), { files: true, symbols: true, query: "pool" });
	assertEqual(parseQuery("f pool"), { files: true, symbols: false, query: "pool" });
	assertEqual(parseQuery("# LspPool"), { files: false, symbols: true, query: "LspPool" });
	assertEqual(parseQuery("fpool"), { files: true, symbols: true, query: "fpool" }, "a prefix needs its space");
});

test("gotoall: symbols open by their workspace path", () => {
	assertEqual(pathOfUri("ied://w1/src/a%20b.cf", "w1"), "src/a b.cf");
	assertEqual(pathOfUri("ied://w2/src/a.cf", "w1"), null, "another workspace");
	assertEqual(pathOfUri("file:///c:/x.h", "w1"), null);
	const at = { line: 3, character: 7 };
	assertEqual(symbolTarget({ location: { uri: "ied://w1/a.cf", range: { start: at, end: at } } }, "w1"), { path: "a.cf", line: 3, col: 7 });
	assertEqual(symbolTarget({ location: { uri: "ied://w1/a.cf" } }, "w1"), { path: "a.cf" }, "a WorkspaceSymbol without a range");
	assertEqual(symbolIconName(5), "symbol-class");
	assertEqual(symbolIconName(99), "symbol-misc");
});

test("gotoall: how well a name matches", () => {
	assertEqual(["pool", "pool.cf", "Pool"].map((name) => matchTier("pool", name)), [0, 0, 0], "the name, its extension aside, any case");
	assertEqual(matchTier("pool", "PoolOptions"), 1, "its start");
	assertEqual(matchTier("pool", "LspPool"), 2, "inside it");
	assertEqual([matchTier("lp", "LspPool"), matchTier("fs", "find_symbol"), matchTier("gta", "go-to-all.js")], [3, 3, 3], "word starts");
	assertEqual(matchTier("pool", "03-project-and-solution-model.md"), 4, "scattered");
	assertEqual(matchTier("", "anything"), 0);
});

test("gotoall: files and symbols ranked together, files found through their folders last", () => {
	const at = (line) => ({ start: { line, character: 7 }, end: { line, character: 14 } });
	const files = [{ path: "src/lsp/pool.cf", score: 1210 }, { path: "docs/03-project-and-solution-model.md", score: 1010 }, { path: "src/lsp/pooled/x.cf", score: 20 }];
	const symbols = [
		{ name: "LspPool", kind: 5, containerName: "Ied", location: { uri: "ied://w1/src/lsp/pool.ch", range: at(9) } },
		{ name: "PoolOptions", kind: 23, location: { uri: "ied://w1/src/gc/pool.ch", range: at(2) } },
		{ name: "Pool", kind: 5, location: { uri: "file:///c:/sdk/pool.h" } },
	];
	const rows = rowsOf(files, symbols, "w1", "pool");
	assertEqual(rows.map((r) => r.label), ["pool.cf", "PoolOptions", "LspPool", "03-project-and-solution-model.md", "x.cf"]);
	assertEqual(rows[2].detail, "Ied - src/lsp/pool.ch");
	assertEqual(rows[2].target, { path: "src/lsp/pool.ch", line: 9, col: 7 });
});
