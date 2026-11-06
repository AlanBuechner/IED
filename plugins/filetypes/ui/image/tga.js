// tga.js - Truevision TGA read into RGBA8, rows top to bottom: color-mapped, true-color and grayscale images, raw or
// run-length coded, 8, 15, 16, 24 and 32 bits a pixel, from either corner.
//     const picture = decodeTga(bytes);   // {width, height, data: Uint8Array, format: "TGA 32-bit RLE"}

const HEADER = 18;

function fail(message) {
	throw new Error("Not a TGA file this viewer reads: " + message + ".");
}

const five = (x) => (x << 3) | (x >> 2);

// Writes the pixel of `depth` bits at `bytes[at]` to `out[o..o+3]` as r, g, b, a.
function writerOf(bytes, depth, alphaBits, gray) {
	if (gray && depth === 8) return (at, out, o) => { out[o] = out[o + 1] = out[o + 2] = bytes[at]; out[o + 3] = 255; };
	if (gray && depth === 16) return (at, out, o) => { out[o] = out[o + 1] = out[o + 2] = bytes[at]; out[o + 3] = bytes[at + 1]; };
	if (gray) return fail(depth + "-bit gray pixels");
	if (depth === 15 || depth === 16) {
		const alpha = depth === 16 && alphaBits > 0;
		return (at, out, o) => {
			const v = bytes[at] | (bytes[at + 1] << 8);
			out[o] = five((v >> 10) & 31);
			out[o + 1] = five((v >> 5) & 31);
			out[o + 2] = five(v & 31);
			out[o + 3] = !alpha || (v & 0x8000) ? 255 : 0;
		};
	}
	if (depth === 24) return (at, out, o) => { out[o] = bytes[at + 2]; out[o + 1] = bytes[at + 1]; out[o + 2] = bytes[at]; out[o + 3] = 255; };
	if (depth === 32) return (at, out, o) => { out[o] = bytes[at + 2]; out[o + 1] = bytes[at + 1]; out[o + 2] = bytes[at]; out[o + 3] = bytes[at + 3]; };
	return fail(depth + "-bit pixels");
}

function headerOf(bytes) {
	if (bytes.length < HEADER) fail("too short");
	const h = {
		idLength: bytes[0], mapType: bytes[1], type: bytes[2],
		mapFirst: bytes[3] | (bytes[4] << 8), mapLength: bytes[5] | (bytes[6] << 8), mapDepth: bytes[7],
		width: bytes[12] | (bytes[13] << 8), height: bytes[14] | (bytes[15] << 8), depth: bytes[16], descriptor: bytes[17],
	};
	h.kind = h.type & 7;
	h.rle = (h.type & 8) !== 0;
	if (h.kind < 1 || h.kind > 3 || h.type > 11 || h.width === 0 || h.height === 0) fail("image type " + h.type);
	if (h.kind === 1 && (h.mapType !== 1 || (h.depth !== 8 && h.depth !== 16))) fail("a color-mapped image without a map");
	return h;
}

// The writer of one stored pixel: through the color map for a color-mapped image.
function pixelWriter(bytes, h) {
	const alphaBits = h.descriptor & 15;
	const mapStart = HEADER + h.idLength;
	if (h.kind !== 1) return writerOf(bytes, h.depth, alphaBits, h.kind === 3);
	const entry = Math.ceil(h.mapDepth / 8);
	const color = writerOf(bytes, h.mapDepth, alphaBits, false);
	return (at, out, o) => {
		const index = (h.depth === 16 ? bytes[at] | (bytes[at + 1] << 8) : bytes[at]) - h.mapFirst;
		if (index >= 0 && index < h.mapLength) color(mapStart + index * entry, out, o);
		else { out[o] = out[o + 1] = out[o + 2] = 0; out[o + 3] = 255; }
	};
}

export function decodeTga(bytes) {
	const h = headerOf(bytes);
	const write = pixelWriter(bytes, h);
	const step = Math.ceil(h.depth / 8);
	const count = h.width * h.height;
	const stored = new Uint8Array(count * 4);
	let at = HEADER + h.idLength + (h.mapType === 1 ? h.mapLength * Math.ceil(h.mapDepth / 8) : 0);
	for (let i = 0; i < count;) {
		if (!h.rle) {
			if (at + step > bytes.length) fail("its pixels end early");
			write(at, stored, i++ * 4);
			at += step;
			continue;
		}
		if (at >= bytes.length) fail("its runs end early");
		const packet = bytes[at++];
		const n = Math.min((packet & 127) + 1, count - i);
		if (at + (packet & 128 ? 1 : n) * step > bytes.length) fail("its runs end early");
		if (packet & 128) {
			write(at, stored, i * 4);
			for (let k = 1; k < n; k++) stored.copyWithin((i + k) * 4, i * 4, i * 4 + 4);
			at += step;
			i += n;
		}
		else for (let k = 0; k < n; k++, at += step) write(at, stored, i++ * 4);
	}
	const data = orient(stored, h.width, h.height, (h.descriptor & 16) !== 0, (h.descriptor & 32) === 0);
	if (h.depth === 32 && (h.descriptor & 15) === 0 && allTransparent(data)) for (let i = 3; i < data.length; i += 4) data[i] = 255;
	const kind = h.kind === 1 ? "color-mapped" : h.kind === 3 ? h.depth + "-bit gray" : h.depth + "-bit";
	return { width: h.width, height: h.height, data, format: "TGA " + kind + (h.rle ? " RLE" : "") };
}

// Rows top to bottom, left to right, whichever corner the file starts at.
function orient(stored, width, height, rightToLeft, bottomUp) {
	if (!rightToLeft && !bottomUp) return stored;
	const data = new Uint8Array(stored.length);
	const row = width * 4;
	for (let y = 0; y < height; y++) {
		const from = (bottomUp ? height - 1 - y : y) * row;
		if (!rightToLeft) {
			data.set(stored.subarray(from, from + row), y * row);
			continue;
		}
		for (let x = 0; x < width; x++) data.set(stored.subarray(from + (width - 1 - x) * 4, from + (width - x) * 4), y * row + x * 4);
	}
	return data;
}

// A 32-bit image that claims no alpha bits and stores zero alpha everywhere was written without alpha: it is opaque.
function allTransparent(data) {
	for (let i = 3; i < data.length; i += 4) if (data[i] !== 0) return false;
	return true;
}
