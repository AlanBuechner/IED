// bc7.js - one BC7 (BPTC unorm) block read into 16 RGBA8 texels: its eight modes, with their partitions, P-bits,
// rotations and index selection, as Direct3D 11 and OpenGL decode them; a reserved mode is transparent black.
//     decodeBc7Block(bytes, at, texels);   // texels: Uint8Array(64)

import { partitionOf, anchorsOf, WEIGHTS, blockWords, bitsAt } from "./bptc.js";

// Subsets, partition bits, rotation bits, index-selection bits, color bits, alpha bits, P-bits per endpoint and per
// subset, index bits and second index bits.
const MODES = [
	{ ns: 3, pb: 4, rb: 0, isb: 0, cb: 4, ab: 0, epb: 1, spb: 0, ib: 3, ib2: 0 },
	{ ns: 2, pb: 6, rb: 0, isb: 0, cb: 6, ab: 0, epb: 0, spb: 1, ib: 3, ib2: 0 },
	{ ns: 3, pb: 6, rb: 0, isb: 0, cb: 5, ab: 0, epb: 0, spb: 0, ib: 2, ib2: 0 },
	{ ns: 2, pb: 6, rb: 0, isb: 0, cb: 7, ab: 0, epb: 1, spb: 0, ib: 2, ib2: 0 },
	{ ns: 1, pb: 0, rb: 2, isb: 1, cb: 5, ab: 6, epb: 0, spb: 0, ib: 2, ib2: 3 },
	{ ns: 1, pb: 0, rb: 2, isb: 0, cb: 7, ab: 8, epb: 0, spb: 0, ib: 2, ib2: 2 },
	{ ns: 1, pb: 0, rb: 0, isb: 0, cb: 7, ab: 7, epb: 1, spb: 0, ib: 4, ib2: 0 },
	{ ns: 2, pb: 6, rb: 0, isb: 0, cb: 5, ab: 5, epb: 1, spb: 0, ib: 2, ib2: 0 },
];

const endpoints = new Int32Array(24);
const pbits = new Int32Array(6);
const primary = new Int32Array(16);
const secondary = new Int32Array(16);

const interpolate = (a, b, w) => ((64 - w) * a + w * b + 32) >> 6;

// An endpoint component of `bits` bits (its P-bit appended when `p` >= 0) widened to 8 bits by repeating its top bits.
function widen(value, bits, p) {
	let v = value;
	let precision = bits;
	if (p >= 0) {
		v = (v << 1) | p;
		precision++;
	}
	v <<= 8 - precision;
	return v | (v >> precision);
}

export function decodeBc7Block(bytes, at, texels) {
	let mode = 0;
	while (mode < 8 && !(bytes[at] & (1 << mode))) mode++;
	if (mode === 8) {
		texels.fill(0);
		return;
	}
	const m = MODES[mode];
	const words = blockWords(bytes, at);
	let pos = mode + 1;
	const read = (n) => {
		const v = bitsAt(words, pos, n);
		pos += n;
		return v;
	};
	const partition = read(m.pb);
	const rotation = read(m.rb);
	const selection = read(m.isb);
	const count = m.ns * 2;
	for (let c = 0; c < 3; c++) for (let i = 0; i < count; i++) endpoints[i * 4 + c] = read(m.cb);
	for (let i = 0; i < count; i++) endpoints[i * 4 + 3] = m.ab ? read(m.ab) : 255;
	if (m.epb) for (let i = 0; i < count; i++) pbits[i] = read(1);
	if (m.spb) for (let s = 0; s < m.ns; s++) pbits[2 * s] = pbits[2 * s + 1] = read(1);
	const hasP = m.epb || m.spb;
	for (let i = 0; i < count; i++) {
		for (let c = 0; c < 3; c++) endpoints[i * 4 + c] = widen(endpoints[i * 4 + c], m.cb, hasP ? pbits[i] : -1);
		if (m.ab) endpoints[i * 4 + 3] = widen(endpoints[i * 4 + 3], m.ab, hasP ? pbits[i] : -1);
	}
	const anchors = anchorsOf(m.ns, partition);
	for (let t = 0; t < 16; t++) primary[t] = read(anchors.includes(t) ? m.ib - 1 : m.ib);
	if (m.ib2) for (let t = 0; t < 16; t++) secondary[t] = read(t === 0 ? m.ib2 - 1 : m.ib2);
	const swapped = m.ib2 && selection === 1;
	const colorWeights = WEIGHTS[swapped ? m.ib2 : m.ib];
	const alphaWeights = WEIGHTS[m.ib2 && !swapped ? m.ib2 : m.ib];
	for (let t = 0; t < 16; t++) {
		const s = partitionOf(m.ns, partition, t);
		const e0 = s * 8;
		const e1 = e0 + 4;
		const cw = colorWeights[swapped ? secondary[t] : primary[t]];
		const aw = alphaWeights[m.ib2 && !swapped ? secondary[t] : primary[t]];
		const o = t * 4;
		texels[o] = interpolate(endpoints[e0], endpoints[e1], cw);
		texels[o + 1] = interpolate(endpoints[e0 + 1], endpoints[e1 + 1], cw);
		texels[o + 2] = interpolate(endpoints[e0 + 2], endpoints[e1 + 2], cw);
		texels[o + 3] = interpolate(endpoints[e0 + 3], endpoints[e1 + 3], aw);
		if (rotation) {
			const other = o + rotation - 1;
			const alpha = texels[o + 3];
			texels[o + 3] = texels[other];
			texels[other] = alpha;
		}
	}
}
