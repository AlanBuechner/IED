// boardchats.js - the boards' chats in the Boards activity (docs/32 "Busy board chats"; owner, 2026-11-16: "it would be
// nice if the planning board would show active chats like the chat tab does"): the activity's button badged with the
// board chats in a turn - how many wait on the owner, else how many run, as the Chat activity's is - and each board's row
// in the Boards window marked while its chat runs or waits. Read from the chat plugin's chats:<wsId> topic (docs/04) and
// folded here: a plugin uses another's protocol, not its modules (AGENTS.md rule 7).

export const CHAT_USE = "board";   // the board's aiUses contribution: what a board's chat is for

// How a board's row shows its chat's state: none while it rests (idle, suspended); else the codicon (turning or not), its
// tone and the words, the Chats list's (chatmodel.js STATE_LOOKS, STATE_LABELS). `attention`: it waits on the owner.
const MARKS = {
	running: { icon: "loading", spin: true, tone: "c-info", label: "Running", attention: false },
	awaiting_approval: { icon: "shield", tone: "c-warn", label: "Needs approval", attention: true },
	awaiting_answer: { icon: "question", tone: "c-warn", label: "Needs an answer", attention: true },
	paused: { icon: "debug-pause", tone: "c-warn", label: "Paused", attention: true },
};

export function chatMark(summary) {
	return summary ? MARKS[summary.state] || null : null;
}

// The Boards activity's badge: null while no board chat is in a turn.
export function boardChatsBadge(chats) {
	const ours = chats.filter((c) => c.use === CHAT_USE);
	const running = ours.filter((c) => c.state === "running").length;
	const waiting = ours.filter((c) => c.state === "awaiting_approval" || c.state === "awaiting_answer").length;
	if (!running && !waiting) return null;
	const parts = [];
	if (running) parts.push(running + (running === 1 ? " chat running" : " chats running"));
	if (waiting) parts.push(waiting + (waiting === 1 ? " needs you" : " need you"));
	return { count: waiting || running, tone: waiting ? "warn" : "info", title: parts.join(", ") };
}

// The workspace's chats, live: `changed(chats)` with every chat's summary (the chat list as the chats:<wsId> topic has
// it) after its snapshot and each change. Returns the function that stops.
export function followChats(ied, wsId, changed) {
	let chats = [];
	return ied.client.subscribe("chats:" + wsId, {
		snap: (data) => {
			chats = (data.chats || []).slice();
			changed(chats);
		},
		evt: (kind, data) => {
			chats = foldChat(chats, kind, data);
			changed(chats);
		},
	});
}

// One chats:<wsId> event folded into the chats: a deleted or archived chat leaves, a created, changed or restored one
// comes in or is replaced. Pure, for the tests.
export function foldChat(chats, kind, data) {
	const others = chats.filter((c) => c.chatId !== data.chatId);
	if (kind === "chat.deleted" || kind === "chat.archived") return others;
	if (kind === "chat.created" || kind === "chat.changed" || kind === "chat.restored") return others.concat(data);
	return chats;
}

// The Boards activity's button follows the workspace's board chats, from one workspace to the next.
export function badgeBoardChats(ied) {
	let stop = null;
	const follow = () => {
		if (stop) stop();
		stop = null;
		ied.activities.badge("ied.board", null);
		const wsId = ied.workspace.wsId;
		if (wsId) stop = followChats(ied, wsId, (chats) => ied.activities.badge("ied.board", boardChatsBadge(chats)));
	};
	if (ied.workspace.on) ied.workspace.on(follow);
	follow();
}
