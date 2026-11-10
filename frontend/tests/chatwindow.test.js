// chatwindow.test.js - the window of a chat the client holds (docs/10 "Chats"): pages joined before and after it, a turn
// cut by a page joined back whole, the window cut to its size from either side, and what streams at the chat's end
// passed over while the window does not hold it.

import { test, assertEqual } from "./harness.js";
import { ChatModel, turnEndHeld, turnStartHeld } from "/plugins/ied.chat/ui/chatmodel.js";

function segments(turnId, from, to) {
	const list = [];
	for (let i = from; i < to; i += 1) list.push({ segId: turnId + "-s" + i, type: "text", done: true, text: "t" + i });
	return list;
}

function ids(model) {
	return model.turns.flatMap((turn) => turn.segments.map((s) => s.segId));
}

// The last page of a chat: u1 whole (3 segments), then u2's last 4 of 10, u2 running.
function lastPage() {
	return {
		chatId: "c1", wsId: "w1", title: "Long", state: "running", hasMore: true,
		turns: [{ turnId: "u2", user: { text: "go" }, firstSeg: 6, segmentCount: 10, segments: segments("u2", 6, 10), stop: null, recoveries: [] }],
	};
}

test("chat window: the page before joins the turn it cut", () => {
	const model = new ChatModel();
	model.snapshot(lastPage());
	const turn = model.turns[0];
	assertEqual([turnStartHeld(turn), turnEndHeld(turn), model.hasMore, model.hasNewer], [false, true, true, false], "the last page holds u2's end, not its start");
	model.prepend({ hasMore: false, turns: [
		{ turnId: "u1", user: { text: "hi" }, segments: segments("u1", 0, 3), stop: "finished", recoveries: [] },
		{ turnId: "u2", user: { text: "go" }, firstSeg: 0, segmentCount: 10, segments: segments("u2", 0, 6), stop: null, recoveries: [] },
	] });
	assertEqual(model.turns.map((t) => t.turnId), ["u1", "u2"], "u2 joined, not repeated");
	assertEqual(ids(model).length, 13);
	assertEqual(ids(model)[3], "u2-s0", "in order");
	assertEqual([turnStartHeld(model.turns[1]), model.hasMore], [true, false], "u2 whole now; nothing before");
});

test("chat window: cut to its size from the start, then from the end", () => {
	const model = new ChatModel();
	model.snapshot({ chatId: "c1", hasMore: false, turns: [
		{ turnId: "u1", user: { text: "a" }, segments: segments("u1", 0, 5), stop: "finished", recoveries: [] },
		{ turnId: "u2", user: { text: "b" }, segments: segments("u2", 0, 8), stop: "finished", recoveries: [] },
	] });
	assertEqual(model.trimOlder(20), false, "within its size: nothing let go");
	assertEqual(model.trimOlder(6), true);
	assertEqual(model.turns.map((t) => [t.turnId, t.firstSeg || 0, t.segmentCount]), [["u2", 2, 8]], "u1 gone, u2 from its third segment");
	assertEqual([ids(model).length, model.hasMore], [6, true]);
	model.trimNewer(4);
	assertEqual(ids(model), ["u2-s2", "u2-s3", "u2-s4", "u2-s5"], "the end let go");
	assertEqual([turnEndHeld(model.turns[0]), model.hasNewer], [false, true], "u2's end is no longer held");
});

test("chat window: the page after joins, and the chat's end streams only once it is held", () => {
	const model = new ChatModel();
	model.snapshot(lastPage());
	model.trimNewer(2);
	assertEqual([ids(model), model.hasNewer], [["u2-s6", "u2-s7"], true]);
	assertEqual(model.event("segment.start", { turnId: "u2", segId: "u2-s10", type: "text" }), {}, "a new segment past the window: passed over");
	assertEqual(model.event("turn.start", { turnId: "u3", user: { text: "next" } }), {}, "a new turn too");
	assertEqual(model.turns.length, 1);
	model.event("segment.delta", { segId: "u2-s7", text: "+" });
	assertEqual(model.segment("u2-s7").segment.text, "t7+", "a segment the window holds still streams");
	model.append({ hasNewer: false, turns: [{ turnId: "u2", user: { text: "go" }, firstSeg: 8, segmentCount: 11, segments: segments("u2", 8, 11), stop: null, recoveries: [] }] });
	assertEqual(ids(model), ["u2-s6", "u2-s7", "u2-s8", "u2-s9", "u2-s10"], "joined to u2");
	assertEqual([model.hasNewer, turnEndHeld(model.lastTurn())], [false, true], "the end held again");
	assertEqual(model.event("segment.start", { turnId: "u2", segId: "u2-s11", type: "text" }).segment.segId, "u2-s11", "what streams now joins");
});
