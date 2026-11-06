// bc6h.js - one BC6H block (BPTC float, unsigned or signed) read into 16 RGBA float texels: its fourteen modes, with
// their scattered endpoint bits, transformed (delta) endpoints and partitions, as Direct3D 11 decodes them; a reserved
// mode is black. Alpha is 1.
//     decodeBc6hBlock(bytes, at, texels, false);   // texels: Float32Array(64)

import { partitionOf, anchorsOf, WEIGHTS, blockWords, bitsAt } from "./bptc.js";
import { halfToFloat } from "./floats.js";

// Each mode's endpoint bits in the order the block stores them after its mode bits: "<endpoint><channel>.<first>-<last>"
// (r0 is region 0's first endpoint, r1 its second, r2 and r3 region 1's; d the partition), the first bit read the
// lowest of its run; and the endpoints' bits: the first's, and the others' (deltas when transformed) per channel.
const MODES = {
	0: { regions: 2, base: 10, deltas: [5, 5, 5], transformed: true, layout: "g2.4 b2.4 b3.4 r0.0-9 g0.0-9 b0.0-9 r1.0-4 g3.4 g2.0-3 g1.0-4 b3.0 g3.0-3 b1.0-4 b3.1 b2.0-3 r2.0-4 b3.2 r3.0-4 b3.3 d.0-4" },
	1: { regions: 2, base: 7, deltas: [6, 6, 6], transformed: true, layout: "g2.5 g3.4 g3.5 r0.0-6 b3.0 b3.1 b2.4 g0.0-6 b2.5 b3.2 g2.4 b0.0-6 b3.3 b3.5 b3.4 r1.0-5 g2.0-3 g1.0-5 g3.0-3 b1.0-5 b2.0-3 r2.0-5 r3.0-5 d.0-4" },
	2: { regions: 2, base: 11, deltas: [5, 4, 4], transformed: true, layout: "r0.0-9 g0.0-9 b0.0-9 r1.0-4 r0.10 g2.0-3 g1.0-3 g0.10 b3.0 g3.0-3 b1.0-3 b0.10 b3.1 b2.0-3 r2.0-4 b3.2 r3.0-4 b3.3 d.0-4" },
	6: { regions: 2, base: 11, deltas: [4, 5, 4], transformed: true, layout: "r0.0-9 g0.0-9 b0.0-9 r1.0-3 r0.10 g3.4 g2.0-3 g1.0-4 g0.10 g3.0-3 b1.0-3 b0.10 b3.1 b2.0-3 r2.0-3 b3.0 b3.2 r3.0-3 g2.4 b3.3 d.0-4" },
	10: { regions: 2, base: 11, deltas: [4, 4, 5], transformed: true, layout: "r0.0-9 g0.0-9 b0.0-9 r1.0-3 r0.10 b2.4 g2.0-3 g1.0-3 g0.10 b3.0 g3.0-3 b1.0-4 b0.10 b2.0-3 r2.0-3 b3.1 b3.2 r3.0-3 b3.4 b3.3 d.0-4" },
	14: { regions: 2, base: 9, deltas: [5, 5, 5], transformed: true, layout: "r0.0-8 b2.4 g0.0-8 g2.4 b0.0-8 b3.4 r1.0-4 g3.4 g2.0-3 g1.0-4 b3.0 g3.0-3 b1.0-4 b3.1 b2.0-3 r2.0-4 b3.2 r3.0-4 b3.3 d.0-4" },
	18: { regions: 2, base: 8, deltas: [6, 5, 5], transformed: true, layout: "r0.0-7 g3.4 b2.4 g0.0-7 b3.2 g2.4 b0.0-7 b3.3 b3.4 r1.0-5 g2.0-3 g1.0-4 b3.0 g3.0-3 b1.0-4 b3.1 b2.0-3 r2.0-5 r3.0-5 d.0-4" },
	22: { regions: 2, base: 8, deltas: [5, 6, 5], transformed: true, layout: "r0.0-7 b3.0 b2.4 g0.0-7 g2.5 g2.4 b0.0-7 g3.5 b3.4 r1.0-4 g3.4 g2.0-3 g1.0-5 g3.0-3 b1.0-4 b3.1 b2.0-3 r2.0-4 b3.2 r3.0-4 b3.3 d.0-4" },
	26: { regions: 2, base: 8, deltas: [5, 5, 6], transformed: true, layout: "r0.0-7 b3.1 b2.4 g0.0-7 b2.5 g2.4 b0.0-7 b3.5 b3.4 r1.0-4 g3.4 g2.0-3 g1.0-4 b3.0 g3.0-3 b1.0-5 b2.0-3 r2.0-4 b3.2 r3.0-4 b3.3 d.0-4" },
	30: { regions: 2, base: 6, deltas: [6, 6, 6], transformed: false, layout: "r0.0-5 g3.4 b3.0 b3.1 b2.4 g0.0-5 g2.5 b2.5 b3.2 g2.4 b0.0-5 g3.5 b3.3 b3.5 b3.4 r1.0-5 g2.0-3 g1.0-5 g3.0-3 b1.0-5 b2.0-3 r2.0-5 r3.0-5 d.0-4" },
	3: { regions: 1, base: 10, deltas: [10, 10, 10], transformed: false, layout: "r0.0-9 g0.0-9 b0.0-9 r1.0-9 g1.0-9 b1.0-9" },
	7: { regions: 1, base: 11, deltas: [9, 9, 9], transformed: true, layout: "r0.0-9 g0.0-9 b0.0-9 r1.0-8 r0.10 g1.0-8 g0.10 b1.0-8 b0.10" },
	11: { regions: 1, base: 12, deltas: [8, 8, 8], transformed: true, layout: "r0.0-9 g0.0-9 b0.0-9 r1.0-7 r0.11-10 g1.0-7 g0.11-10 b1.0-7 b0.11-10" },
	15: { regions: 1, base: 16, deltas: [4, 4, 4], transformed: true, layout: "r0.0-9 g0.0-9 b0.0-9 r1.0-3 r0.15-10 g1.0-3 g0.15-10 b1.0-3 b0.15-10" },
};

const PARTITION_SLOT = 12;

// The layout as one [slot, bit] per stored bit: slot endpoint * 3 + channel, or PARTITION_SLOT.
function compile(layout) {
	const steps = [];
	for (const token of layout.split(" ")) {
		const [name, range] = token.split(".");
		const slot = name === "d" ? PARTITION_SLOT : Number(name[1]) * 3 + "rgb".indexOf(name[0]);
		const [first, last] = range.includes("-") ? range.split("-").map(Number) : [Number(range), Number(range)];
		const step = first <= last ? 1 : -1;
		for (let bit = first; bit !== last + step; bit += step) steps.push(slot, bit);
	}
	return steps;
}
for (const mode of Object.values(MODES)) mode.steps = compile(mode.layout);

const fields = new Int32Array(13);
const ends = new Int32Array(12);

const signExtend = (v, bits) => (v << (32 - bits)) >> (32 - bits);

function unquantize(v, bits, signed) {
	if (!signed) {
		if (bits >= 15 || v === 0) return v;
		if (v === (1 << bits) - 1) return 0xffff;
		return ((v << 16) + 0x8000) >> bits;
	}
	if (bits >= 16) return v;
	const negative = v < 0;
	const magnitude = negative ? -v : v;
	let q = 0;
	if (magnitude === 0) q = 0;
	else if (magnitude >= (1 << (bits - 1)) - 1) q = 0x7fff;
	else q = ((magnitude << 15) + 0x4000) >> (bits - 1);
	return negative ? -q : q;
}

// An interpolated value as the half float it stands for.
function finish(c, signed) {
	if (!signed) return halfToFloat((c * 31) >> 6);
	return halfToFloat(c < 0 ? (((-c * 31) >> 5) | 0x8000) : (c * 31) >> 5);
}

// The endpoints read from `fields`, resolved (deltas added, signs extended) and unquantized into `ends`.
function resolveEndpoints(mode, signed) {
	const count = mode.regions * 2;
	const mask = (1 << mode.base) - 1;
	for (let c = 0; c < 3; c++) {
		const base = signed ? signExtend(fields[c], mode.base) : fields[c];
		ends[c] = base;
		for (let i = 1; i < count; i++) {
			let v = fields[i * 3 + c];
			if (mode.transformed) v = (fields[c] + signExtend(v, mode.deltas[c])) & mask;
			ends[i * 3 + c] = signed ? signExtend(v, mode.base) : v;
		}
		for (let i = 0; i < count; i++) ends[i * 3 + c] = unquantize(ends[i * 3 + c], mode.base, signed);
	}
}

export function decodeBc6hBlock(bytes, at, texels, signed) {
	const words = blockWords(bytes, at);
	let key = bitsAt(words, 0, 2);
	let pos = 2;
	if (key > 1) {
		key |= bitsAt(words, 2, 3) << 2;
		pos = 5;
	}
	const mode = MODES[key];
	if (!mode) {
		for (let t = 0; t < 16; t++) texels.set([0, 0, 0, 1], t * 4);
		return;
	}
	fields.fill(0);
	const steps = mode.steps;
	for (let i = 0; i < steps.length; i += 2) fields[steps[i]] |= bitsAt(words, pos++, 1) << steps[i + 1];
	resolveEndpoints(mode, signed);
	const partition = fields[PARTITION_SLOT];
	const indexBits = mode.regions === 2 ? 3 : 4;
	const weights = WEIGHTS[indexBits];
	const anchors = anchorsOf(mode.regions, partition);
	for (let t = 0; t < 16; t++) {
		const bits = anchors.includes(t) ? indexBits - 1 : indexBits;
		const w = weights[bitsAt(words, pos, bits)];
		pos += bits;
		const e = partitionOf(mode.regions, partition, t) * 6;
		for (let c = 0; c < 3; c++) texels[t * 4 + c] = finish(((64 - w) * ends[e + c] + w * ends[e + 3 + c] + 32) >> 6, signed);
		texels[t * 4 + 3] = 1;
	}
}
