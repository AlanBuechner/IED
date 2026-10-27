// packages.test.js - the Package Manager's pure parts (plugins/nuke, docs/31 section 8): one repository's spellings, the
// update a package has, the requirement a chosen version writes, which requirement brings a package, and its rows.

import { test, assertEqual } from "./harness.js";
import {
	addWords, compatible, filtered, installedRows, packageName, requirementFor, requirementOf, rowBringing, sameRepository, sourceOf, treeLink, updateOf, updateRows,
} from "/plugins/ied.nuke/ui/packagemodel.js";

const BODY = {
	sources: [{ name: "claymore", url: "https://github.com/AlanBuechner/Claymore.git", web: "https://github.com/AlanBuechner/Claymore" },
		{ name: "utils", url: "https://github.com/x/utils", web: "https://github.com/x/utils", transitive: true }],
	requirements: [{ name: "Windowing", path: "Windowing", source: "https://github.com/AlanBuechner/Claymore.git/Windowing", url: "https://github.com/AlanBuechner/Claymore.git",
		requirement: "^0.1", kind: "version", by: "project Game", project: "Game" }],
	packages: [
		{ source: "claymore", url: "https://github.com/alanbuechner/claymore", path: "Windowing", version: "0.1.0", uses: ["Windowing", "Windowing/Win32"], compatible: "0.1.1", newest: "0.2.0", packages: [{ path: "Windowing", description: "Windows and raw input", projects: ["Windowing"] }] },
		{ source: "claymore", url: "https://github.com/AlanBuechner/Claymore.git", path: "Runtime", version: "0.1.0", uses: ["Runtime"], newest: "1.0.0", packages: [] },
		{ source: "utils", url: "https://github.com/x/utils", path: "", version: "2.3.0", uses: [""], packages: [{ path: "", description: "small things", projects: [] }] },
	],
	checkouts: [],
};

test("packages: one repository however it is spelled", () => {
	assertEqual(sameRepository("https://github.com/A/B.git", "https://github.com/a/b/"), true, "case, .git and a slash");
	assertEqual(sameRepository("C:\\work\\lib", "c:/work/lib"), true, "a local path");
	assertEqual(sameRepository("https://github.com/a/b", "https://github.com/a/c"), false);
});

test("packages: names and updates", () => {
	assertEqual(packageName("claymore", "Renderer/RHI"), "claymore:Renderer/RHI");
	assertEqual(packageName("utils", ""), "utils", "a repository that is one package");
	assertEqual(updateOf(BODY.packages[0]), { version: "0.1.1", breaking: false }, "update reaches the compatible one");
	assertEqual(updateOf(BODY.packages[1]), { version: "1.0.0", breaking: true }, "only a new major");
	assertEqual(updateOf(BODY.packages[2]), null, "at its newest");
	assertEqual(updateRows(BODY).map((row) => row.key), ["claymore:Runtime", "claymore:Windowing"]);
});

test("packages: Cargo's caret", () => {
	assertEqual(compatible("1.2.3", "1.9.0"), true);
	assertEqual(compatible("1.2.3", "2.0.0"), false);
	assertEqual(compatible("0.2.1", "0.2.7"), true, "0.x: the minor is the major");
	assertEqual(compatible("0.2.1", "0.3.0"), false);
	assertEqual(compatible("0.0.3", "0.0.4"), false, "0.0.x: every patch breaks");
});

test("packages: the requirement a chosen version writes", () => {
	const versions = ["0.2.0", "0.1.2", "0.1.1", "0.1.0"];
	assertEqual(requirementFor(versions, "0.2.0"), "^0.2.0", "the newest: later fixes still come");
	assertEqual(requirementFor(versions, "0.1.2"), "^0.1.2", "the newest of its line");
	assertEqual(requirementFor(versions, "0.1.1"), "=0.1.1", "an older one is pinned, or ^ would keep 0.1.2");
});

test("packages: which requirement brings a package, and the words that change it", () => {
	assertEqual(requirementOf(BODY, BODY.packages[0]).requirement, "^0.1", "a sub-package's line, the URL spelled differently");
	assertEqual(requirementOf(BODY, BODY.packages[1]), null, "only other packages need it");
	assertEqual(sourceOf(BODY.packages[0]), "https://github.com/alanbuechner/claymore/Windowing", "the repository and the package's folder");
	assertEqual(sourceOf(BODY.packages[2]), "https://github.com/x/utils", "a repository that is one package");
	assertEqual(addWords("https://h/o/r.git/A", "^1.2.0", "Game"), ["add", "https://h/o/r.git/A", "^1.2.0", "--project", "Game"]);
	assertEqual(addWords("https://h/o/r.git/A", "=1.1.0"), ["add", "https://h/o/r.git/A", "=1.1.0"], "Nuke picks the project");
	assertEqual(rowBringing(BODY, "Windowing/Win32").key, "claymore:Windowing", "a Solution Explorer package node");
});

test("packages: rows, search and links", () => {
	assertEqual(installedRows(BODY).map((row) => row.key), ["claymore:Runtime", "claymore:Windowing", "utils"]);
	assertEqual(filtered(installedRows(BODY), "raw INPUT").map((row) => row.key), ["claymore:Windowing"], "a description matches");
	assertEqual(treeLink("https://github.com/o/r", "Renderer/RHI", "1.2.0"), "https://github.com/o/r/tree/Renderer/RHI/v1.2.0/Renderer/RHI");
	assertEqual(treeLink("https://github.com/o/r", "", "1.2.0"), "https://github.com/o/r/tree/v1.2.0");
	assertEqual(treeLink(null, "A", "1.0.0"), null, "a local source has no page");
});
