// git.test.js - the Git activity's pure parts (plugins/git): lanes laid out from parents, a commit's labels, dates, a
// diff's line numbers and its side-by-side pairs, an untracked file as a diff, branch names and the status bar text.

import { test, assertEqual } from "./harness.js";
import { layoutGraph } from "/plugins/ied.git/ui/graph.js";
import { RepoModel, labelsByOid, shortDate, statusClass, syncText } from "/plugins/ied.git/ui/model.js";
import { numbered, sideBySide } from "/plugins/ied.git/ui/diffview.js";
import { wholeFileDiff } from "/plugins/ied.git/ui/diffpane.js";
import { branchProblem } from "/plugins/ied.git/ui/activity.js";
import { gitStatusText } from "/plugins/ied.git/ui/index.js";

test("git: a straight history is one lane", () => {
	const { rows, width } = layoutGraph([{ oid: "c", parents: ["b"] }, { oid: "b", parents: ["a"] }, { oid: "a", parents: [] }]);
	assertEqual(rows.map((r) => r.lane), [0, 0, 0]);
	assertEqual(width, 1);
	assertEqual([rows[0].in, rows[1].in.length, rows[2].out], [[], 1, []], "nothing above the first, nothing below the root");
});

test("git: a branch and its merge", () => {
	// m merges f into b's line: m -> (b, f); f -> a; b -> a
	const { rows, width } = layoutGraph([
		{ oid: "m", parents: ["b", "f"] },
		{ oid: "f", parents: ["a"] },
		{ oid: "b", parents: ["a"] },
		{ oid: "a", parents: [] },
	]);
	assertEqual(rows.map((r) => r.lane), [0, 1, 0, 0]);
	assertEqual(width, 2);
	assertEqual(rows[0].out.map((o) => o.lane), [0, 1], "the merge leaves to both parents");
	assertEqual(rows[2].pass.map((p) => p.lane), [1], "the branch runs past b");
	assertEqual(rows[3].in.map((i) => i.lane), [0, 1], "both lines end at their fork point");
	assertEqual(rows[1].color === rows[0].color, false, "a new lane gets its own color");
});

test("git: paging continues the lanes", () => {
	const first = layoutGraph([{ oid: "c", parents: ["b"] }, { oid: "x", parents: ["a"] }]);
	const next = layoutGraph([{ oid: "b", parents: ["a"] }, { oid: "a", parents: [] }], first.state);
	assertEqual(next.rows.map((r) => r.lane), [0, 0]);
	assertEqual(next.rows[1].in.map((i) => i.lane), [0, 1], "x's lane ends at a on the next page");
});

test("git: labels, dates and counts", () => {
	const refs = {
		branches: [{ name: "main", oid: "a", current: true }, { name: "fix", oid: "b", current: false }],
		remotes: [{ name: "origin", branches: [{ name: "main", oid: "a" }] }],
		tags: [{ name: "v1", oid: "a" }],
		worktrees: [{ path: "C:/w/fix", branch: "fix", current: false }],
	};
	const labels = labelsByOid(refs, { oid: "a", detached: false });
	assertEqual(labels.get("a").map((l) => l.kind + ":" + l.name), ["head:main", "remote:origin/main", "tag:v1"]);
	assertEqual(labels.get("b").map((l) => l.kind), ["branch", "worktree"], "a branch checked out in another worktree");
	const now = new Date(2026, 9, 21, 12, 0).getTime();
	assertEqual(shortDate(new Date(2026, 9, 21, 9, 5).getTime(), now), "09:05");
	assertEqual(shortDate(new Date(2026, 9, 20, 18, 5).getTime(), now), "Yesterday 18:05");
	assertEqual(shortDate(new Date(2026, 9, 17, 8, 0).getTime(), now), "Oct 17");
	assertEqual(shortDate(new Date(2025, 0, 2, 8, 0).getTime(), now), "Jan 2, 2025");
	assertEqual(["A", "?", "D", "M", "UU"].map(statusClass), ["st-a", "st-a", "st-d", "st-m", "st-u"]);
	const model = new RepoModel();
	model.snapshot({ head: null, changes: { staged: [{ path: "a" }], unstaged: [{ path: "a" }, { path: "b" }], untracked: [], conflicted: [] }, refs: null, op: null });
	assertEqual(model.localCount(), 2, "a path staged and changed again counts once");
	assertEqual(syncText({ ahead: 3, behind: 1 }), "3 to push, 1 to pull");
});

test("git: operations stream into the model", () => {
	const model = new RepoModel();
	model.event("op.progress", { opId: "op1", kind: "push", line: "git push --progress" });
	model.event("op.progress", { opId: "op1", kind: "push", line: "Writing objects: 50% (1/2)", percent: 50 });
	assertEqual([model.op.title, model.op.lines.length, model.op.percent], ["git push --progress", 2, 50]);
	model.event("op.end", { opId: "op1", kind: "push", ok: true });
	assertEqual([model.op, model.lastOp.ok], [null, true]);
});

test("git: diff lines, pairs and a whole file", () => {
	const hunk = { oldStart: 10, newStart: 10, lines: [" a", "-b", "-c", "+B", " d", "+e"] };
	assertEqual(numbered(hunk).map((l) => [l.oldNo, l.newNo]), [[10, 10], [11, null], [12, null], [null, 11], [13, 12], [null, 13]]);
	const pairs = sideBySide(hunk).map((r) => [r.left ? r.left.text : null, r.right ? r.right.text : null]);
	assertEqual(pairs, [["a", "a"], ["b", "B"], ["c", null], ["d", "d"], [null, "e"]]);
	assertEqual(numbered({ oldStart: 1, newStart: 1, lines: ["+x\r"] })[0].text, "x", "a CRLF file's lines show without the CR");
	const whole = wholeFileDiff("n.txt", "one\ntwo\n");
	assertEqual([whole.hunks[0].header, whole.hunks[0].lines], ["@@ -0,0 +1,2 @@", ["+one", "+two"]]);
	assertEqual(wholeFileDiff("e.txt", "").hunks, []);
});

test("git: branch names and the status bar", () => {
	assertEqual(["feature/x", "bad name", "-x", "a..b", "x.lock", "ok_1"].map(branchProblem).map(Boolean), [false, true, true, true, true, false]);
	assertEqual(gitStatusText({ branch: "main", oid: "abcdef12", detached: false, ahead: 2, behind: 0 }), { name: "main", ahead: 2, behind: 0, title: "2 to push" });
	assertEqual(gitStatusText({ branch: "", oid: "abcdef12", detached: true, ahead: 0, behind: 0 }).name, "abcdef1");
	assertEqual(gitStatusText(null), null);
});
