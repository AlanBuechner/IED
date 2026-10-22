// counterpartfollow.js - a header or code file tethered beside its file (Open Header / Code File to the Side) follows the
// file its partner's group shows, as a Markdown preview does (docs/15 "Tethered tabs"): when either group shows another
// file of a `headerSourcePairs` contribution that says `follow`, the other group shows that file's counterpart, in the
// old one's place and tethered to it. A file without a counterpart leaves the pair as it was.

import { extensionOf, findCounterpart } from "./headersource.js";

// Whether tab `ref` is a document of a pair that follows.
export function follows(ref, pairs) {
	if (!ref || !ref.startsWith("doc:")) return false;
	const extension = extensionOf(ref.slice("doc:".length));
	return pairs.some((pair) => pair.follow && (pair.extensions || []).some((e) => e.toLowerCase() === extension));
}

// The pairs to move: a following document tethered to one in another group, whose own group shows another following
// document not tethered yet - [{side, partner, next}], `partner` to become `next`'s counterpart.
export function moves(state, partnersOf, pairs) {
	const out = [];
	const taken = new Set();
	for (const side of state.open.filter((ref) => follows(ref, pairs))) {
		const group = state.groups.find((g) => g.tabs.includes(side));
		const next = group ? group.active : null;
		if (!next || next === side || !follows(next, pairs) || partnersOf(next).length) continue;
		for (const partner of partnersOf(side)) {
			if (taken.has(partner) || group.tabs.includes(partner) || !follows(partner, pairs)) continue;
			taken.add(partner);
			out.push({ side, partner, next });
		}
	}
	return out;
}

const keyOf = (move) => move.side + "\n" + move.partner + "\n" + move.next;

export function followCounterparts(ied) {
	const pairs = () => ied.contributions("headerSourcePairs").map((c) => c.value);
	const current = () => moves(ied.tabs.state(), (ref) => ied.tabs.partners(ref), pairs());
	const missing = new Set();   // moves whose file has no counterpart, until they stop being asked for
	let running = false;
	let again = false;
	const run = async () => {
		if (running) {
			again = true;
			return;
		}
		running = true;
		try {
			const asked = current();
			for (const key of [...missing]) if (!asked.some((move) => keyOf(move) === key)) missing.delete(key);
			for (const move of asked.filter((m) => !missing.has(keyOf(m)))) {
				const found = await findCounterpart(ied.client, ied.workspace.wsId, move.next.slice("doc:".length), pairs());
				if (!current().some((m) => keyOf(m) === keyOf(move))) continue;
				if (!found || !apply(ied, move, "doc:" + found)) missing.add(keyOf(move));
			}
		}
		finally {
			running = false;
		}
		if (again) {
			again = false;
			run();
		}
	};
	let queued = false;
	return ied.tabs.onChange(() => {
		if (queued) return;
		queued = true;
		setTimeout(() => {
			queued = false;
			run();
		}, 0);
	});
}

// `move.partner` becomes `counterpart` beside `move.next`; one open already is tethered where it is, unless it shares
// `next`'s group. False when nothing could move.
function apply(ied, move, counterpart) {
	const state = ied.tabs.state();
	if (state.open.includes(counterpart)) {
		const group = state.groups.find((g) => g.tabs.includes(counterpart));
		if (!group || group.tabs.includes(move.next)) return false;
	}
	else {
		ied.tabs.replace(move.partner, counterpart);
		ied.tabs.untether(counterpart);
	}
	ied.tabs.tether(move.next, counterpart);
	ied.tabs.open(counterpart, { preview: true });
	return true;
}
