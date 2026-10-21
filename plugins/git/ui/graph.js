// graph.js - the commit graph's lanes (docs/10 "Git": the client lays them out from the parents, so paging only
// appends). Rows come newest first; each row says which lane its commit sits in and the line pieces drawn in it, so a
// virtualized list draws a row at a time. A lane keeps its color while it lives.

export const LANE_COLORS = 6;

// commits: [{oid, parents}] newest first; `state` continues an earlier page's layout. Returns {rows, width, state}: a row is {lane, color, pass, in, out}, where `pass` are
// lanes running straight through ([{lane, color}]), `in` the lanes that end at the commit (from the row's top) and
// `out` those leaving it (to the row's bottom); `width` is the most lanes any row uses.
export function layoutGraph(commits, state) {
	const lanes = state ? state.lanes.slice() : [];   // [{oid, color}] or null: what each lane waits for
	let nextColor = state ? state.nextColor : 0;
	const rows = [];
	let width = 0;
	const take = (oid) => {
		let free = lanes.indexOf(null);
		if (free < 0) free = lanes.push(null) - 1;
		lanes[free] = { oid, color: nextColor };
		nextColor = (nextColor + 1) % LANE_COLORS;
		return free;
	};
	for (const commit of commits) {
		const waiting = [];
		lanes.forEach((slot, index) => {
			if (slot && slot.oid === commit.oid) waiting.push(index);
		});
		const lane = waiting.length ? waiting[0] : take(commit.oid);
		const color = lanes[lane].color;
		const row = { lane, color, pass: [], in: [], out: [] };
		for (const index of waiting) row.in.push({ lane: index, color: lanes[index].color });
		lanes.forEach((slot, index) => {
			if (slot && index !== lane && !waiting.includes(index)) row.pass.push({ lane: index, color: slot.color });
		});
		for (const index of waiting) if (index !== lane) lanes[index] = null;
		const parents = commit.parents || [];
		if (!parents.length) lanes[lane] = null;
		parents.forEach((parent, index) => {
			if (index === 0) {
				lanes[lane] = { oid: parent, color };
				row.out.push({ lane, color });
				return;
			}
			const existing = lanes.findIndex((slot) => slot && slot.oid === parent);
			const target = existing >= 0 ? existing : take(parent);
			row.out.push({ lane: target, color: lanes[target].color });
		});
		while (lanes.length && lanes[lanes.length - 1] === null) lanes.pop();
		width = Math.max(width, lane + 1, ...row.pass.map((p) => p.lane + 1), ...row.out.map((p) => p.lane + 1), ...row.in.map((p) => p.lane + 1));
		rows.push(row);
	}
	return { rows, width, state: { lanes, nextColor } };
}

// The SVG of one row: its lines, then the commit's dot (a ring for HEAD, dashed for local changes).
export function rowSvg(row, { rowHeight, laneWidth, x0, lanes = 1, head = false, local = false, merge = false }) {
	const x = (lane) => x0 + lane * laneWidth;
	const mid = rowHeight / 2;
	const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
	svg.setAttribute("class", "git-lanes");
	svg.setAttribute("width", String(x0 * 2 + Math.max(0, lanes - 1) * laneWidth));
	svg.setAttribute("height", String(rowHeight));
	const path = (d, color, dashed) => {
		const el = document.createElementNS("http://www.w3.org/2000/svg", "path");
		el.setAttribute("d", d);
		el.setAttribute("class", "l" + color + (dashed ? " dash" : ""));
		svg.append(el);
	};
	for (const p of row.pass) path("M" + x(p.lane) + " 0V" + rowHeight, p.color, false);
	for (const p of row.in) {
		if (p.lane === row.lane) path("M" + x(p.lane) + " 0V" + mid, p.color, false);
		else path("M" + x(p.lane) + " 0C" + x(p.lane) + " " + mid / 2 + " " + x(row.lane) + " " + mid / 2 + " " + x(row.lane) + " " + mid, p.color, false);
	}
	for (const p of row.out) {
		const color = p.lane === row.lane ? row.color : p.color;
		if (p.lane === row.lane) path("M" + x(row.lane) + " " + mid + "V" + rowHeight, color, local);
		else path("M" + x(row.lane) + " " + mid + "C" + x(row.lane) + " " + (mid * 1.5) + " " + x(p.lane) + " " + (mid * 1.5) + " " + x(p.lane) + " " + rowHeight, color, local);
	}
	const dot = (r, cls) => {
		const el = document.createElementNS("http://www.w3.org/2000/svg", "circle");
		el.setAttribute("cx", String(x(row.lane)));
		el.setAttribute("cy", String(mid));
		el.setAttribute("r", String(r));
		el.setAttribute("class", cls);
		svg.append(el);
	};
	if (local) dot(4.5, "dot-local");
	else if (head) {
		dot(5.5, "dot-head l" + row.color);
		dot(2.5, "fill l" + row.color);
	}
	else dot(merge ? 3 : 4, "fill l" + row.color);
	return svg;
}
