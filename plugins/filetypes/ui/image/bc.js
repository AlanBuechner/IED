// bc.js - the 4x4 block formats of Direct3D read into RGBA8: BC1 (DXT1), BC2 (DXT3), BC3 (DXT5), BC4 and BC5 (one and
// two channels, unsigned or signed; a signed value v shows as (v + 1) / 2). A one-channel picture shows as gray, a
// two-channel one in red and green.
//     const rgba = decodeBlocks(bytes, offset, width, height, "bc3");

export const BLOCK_BYTES = { bc1: 8, bc2: 16, bc3: 16, bc4: 8, bc4s: 8, bc5: 16, bc5s: 16, bc6h: 16, bc6hs: 16, bc7: 16 };

const expand5 = (v) => (v << 3) | (v >> 2);
const expand6 = (v) => (v << 2) | (v >> 4);

// The four colors of a BC1 color block at `at` into `palette` (16 bytes); `threeColor` lets c0 <= c1 mean BC1's
// three colors and transparent black, as only BC1 does.
function colorPalette(bytes, at, palette, threeColor) {
	const c0 = bytes[at] | (bytes[at + 1] << 8);
	const c1 = bytes[at + 2] | (bytes[at + 3] << 8);
	const r0 = expand5(c0 >> 11), g0 = expand6((c0 >> 5) & 63), b0 = expand5(c0 & 31);
	const r1 = expand5(c1 >> 11), g1 = expand6((c1 >> 5) & 63), b1 = expand5(c1 & 31);
	palette[0] = r0; palette[1] = g0; palette[2] = b0; palette[3] = 255;
	palette[4] = r1; palette[5] = g1; palette[6] = b1; palette[7] = 255;
	if (c0 > c1 || !threeColor) {
		palette[8] = ((2 * r0 + r1) / 3) | 0; palette[9] = ((2 * g0 + g1) / 3) | 0; palette[10] = ((2 * b0 + b1) / 3) | 0; palette[11] = 255;
		palette[12] = ((r0 + 2 * r1) / 3) | 0; palette[13] = ((g0 + 2 * g1) / 3) | 0; palette[14] = ((b0 + 2 * b1) / 3) | 0; palette[15] = 255;
	}
	else {
		palette[8] = (r0 + r1) >> 1; palette[9] = (g0 + g1) >> 1; palette[10] = (b0 + b1) >> 1; palette[11] = 255;
		palette[12] = 0; palette[13] = 0; palette[14] = 0; palette[15] = 0;
	}
}

// The color block at `at` into the 4x4 RGBA `texels` (64 bytes).
function colorBlock(bytes, at, texels, palette, threeColor) {
	colorPalette(bytes, at, palette, threeColor);
	const indices = bytes[at + 4] | (bytes[at + 5] << 8) | (bytes[at + 6] << 16) | (bytes[at + 7] << 24);
	for (let t = 0; t < 16; t++) {
		const p = ((indices >>> (2 * t)) & 3) * 4;
		texels[t * 4] = palette[p]; texels[t * 4 + 1] = palette[p + 1]; texels[t * 4 + 2] = palette[p + 2]; texels[t * 4 + 3] = palette[p + 3];
	}
}

// A BC3 alpha / BC4 block at `at`: its 16 values (0..255) into `out` at `channel`, every fourth byte from it.
const values = new Float64Array(8);

function valueBlock(bytes, at, out, channel, signed) {
	let a0 = bytes[at];
	let a1 = bytes[at + 1];
	if (signed) {
		a0 = Math.max(-127, (a0 << 24) >> 24);
		a1 = Math.max(-127, (a1 << 24) >> 24);
	}
	values[0] = a0;
	values[1] = a1;
	if (a0 > a1) for (let i = 1; i < 7; i++) values[i + 1] = ((7 - i) * a0 + i * a1) / 7;
	else {
		for (let i = 1; i < 5; i++) values[i + 1] = ((5 - i) * a0 + i * a1) / 5;
		values[6] = signed ? -127 : 0;
		values[7] = signed ? 127 : 255;
	}
	for (let i = 0; i < 8; i++) values[i] = signed ? Math.round((values[i] + 127) * 255 / 254) : values[i] | 0;
	const low = bytes[at + 2] | (bytes[at + 3] << 8) | (bytes[at + 4] << 16);
	const high = bytes[at + 5] | (bytes[at + 6] << 8) | (bytes[at + 7] << 16);
	for (let t = 0; t < 16; t++) {
		const index = t < 8 ? (low >> (3 * t)) & 7 : (high >> (3 * (t - 8))) & 7;
		out[t * 4 + channel] = values[index];
	}
}

function explicitAlpha(bytes, at, texels) {
	for (let t = 0; t < 16; t++) {
		const nibble = (bytes[at + (t >> 1)] >> ((t & 1) * 4)) & 15;
		texels[t * 4 + 3] = nibble * 17;
	}
}

// One block of `format` at `at` into `texels` (RGBA8, 4x4).
function decodeBlock(format, bytes, at, texels, palette) {
	switch (format) {
		case "bc1": return colorBlock(bytes, at, texels, palette, true);
		case "bc2": colorBlock(bytes, at + 8, texels, palette, false); return explicitAlpha(bytes, at, texels);
		case "bc3": colorBlock(bytes, at + 8, texels, palette, false); return valueBlock(bytes, at, texels, 3, false);
		case "bc4": case "bc4s":
			valueBlock(bytes, at, texels, 0, format === "bc4s");
			for (let t = 0; t < 16; t++) { texels[t * 4 + 1] = texels[t * 4 + 2] = texels[t * 4]; texels[t * 4 + 3] = 255; }
			return undefined;
		case "bc5": case "bc5s":
			valueBlock(bytes, at, texels, 0, format === "bc5s");
			valueBlock(bytes, at + 8, texels, 1, format === "bc5s");
			for (let t = 0; t < 16; t++) { texels[t * 4 + 2] = 0; texels[t * 4 + 3] = 255; }
			return undefined;
		default: throw new Error("No decoder for " + format + ".");
	}
}

// The picture of `width` x `height` stored as `format` blocks from `offset`: RGBA8 rows, top to bottom. `blockFn`, when
// given, decodes a block instead (BC6H, BC7): (bytes, at, texels) with texels a Uint8Array or Float32Array of 64.
export function decodeBlocks(bytes, offset, width, height, format, blockFn, texelType) {
	const Type = texelType || Uint8Array;
	const out = new Type(width * height * 4);
	const texels = new Type(64);
	const palette = new Uint8Array(16);
	const across = Math.max(1, (width + 3) >> 2);
	const down = Math.max(1, (height + 3) >> 2);
	const size = BLOCK_BYTES[format];
	if (offset + across * down * size > bytes.length) throw new Error("The texture's data ends early.");
	for (let by = 0; by < down; by++) {
		for (let bx = 0; bx < across; bx++) {
			const at = offset + (by * across + bx) * size;
			if (blockFn) blockFn(bytes, at, texels);
			else decodeBlock(format, bytes, at, texels, palette);
			for (let y = 0; y < 4; y++) {
				const py = by * 4 + y;
				if (py >= height) break;
				const columns = Math.min(4, width - bx * 4);
				out.set(texels.subarray(y * 16, y * 16 + columns * 4), (py * width + bx * 4) * 4);
			}
		}
	}
	return out;
}
