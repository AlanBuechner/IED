// follow.js - a preview beside its file follows the Markdown file its group shows (docs/15 "Tethered tabs"), as VS Code's
// does: when another .md file becomes the active tab of the group that holds the previewed file, the preview shows that
// file instead, in its place, tethered to it. A tab of another kind leaves the preview on the last Markdown file.

const MARKDOWN = /\.(md|markdown)$/i;

// What to do now, from `state` (ied.tabs.state()) and `partnersOf(ref)`: [{from, to, lead}], each preview to replace by
// the preview of `lead`.
export function moves(state, partnersOf) {
	const out = [];
	const taken = new Set(state.open);
	for (const preview of state.open.filter((ref) => ref.startsWith("mdpreview:"))) {
		const lead = "doc:" + preview.slice("mdpreview:".length);
		if (!partnersOf(preview).includes(lead)) continue;
		const group = state.groups.find((g) => g.tabs.includes(lead));
		const next = group ? group.active : null;
		if (!next || next === lead || !next.startsWith("doc:") || !MARKDOWN.test(next)) continue;
		const to = "mdpreview:" + next.slice("doc:".length);
		if (taken.has(to)) continue;
		taken.add(to);
		out.push({ from: preview, to, lead: next });
	}
	return out;
}

// Keeps the previews following, a moment after each change of the tabs (a replace changes them again).
export function followPreviews(ied) {
	let queued = false;
	const run = () => {
		queued = false;
		for (const move of moves(ied.tabs.state(), (ref) => ied.tabs.partners(ref))) {
			ied.tabs.replace(move.from, move.to);
			ied.tabs.untether(move.to);
			ied.tabs.tether(move.lead, move.to, { closes: true });
		}
	};
	return ied.tabs.onChange(() => {
		if (queued) return;
		queued = true;
		setTimeout(run, 0);
	});
}
