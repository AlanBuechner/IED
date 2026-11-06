// hdr.js - a Radiance HDR picture (.hdr, RGBE) read into linear RGBA floats, rows top to bottom: flat or run-length
// coded scanlines, either vertical order.
//     const picture = decodeHdr(bytes);   // {width, height, data: Float32Array, hdr: true, format: "Radiance HDR"}

function fail(message) {
	throw new Error("Not a Radiance HDR file this viewer reads: " + message + ".");
}

// The header's lines and the resolution line, and where the pixels start.
function readHeader(bytes) {
	let at = 0;
	const line = () => {
		let text = "";
		while (at < bytes.length && bytes[at] !== 10) text += String.fromCharCode(bytes[at++]);
		at++;
		return text;
	};
	const magic = line();
	if (!magic.startsWith("#?")) fail("no #?RADIANCE line");
	let format = "32-bit_rle_rgbe";
	for (let text = line(); text !== ""; text = line()) {
		if (at >= bytes.length) fail("its header does not end");
		if (text.startsWith("FORMAT=")) format = text.slice(7).trim();
	}
	if (format !== "32-bit_rle_rgbe") fail("the format " + format);
	const size = /^([-+])Y (\d+) \+X (\d+)$/.exec(line().trim());
	if (!size) fail("a resolution line other than -Y H +X W");
	return { at, width: +size[3], height: +size[2], bottomUp: size[1] === "+" };
}

// One scanline's RGBE bytes from `at` into `row` (width * 4); the position after it.
function readScanline(bytes, at, row, width) {
	if (width < 8 || width > 0x7fff || bytes[at] !== 2 || bytes[at + 1] !== 2 || (bytes[at + 2] & 0x80)) {
		if (at + width * 4 > bytes.length) fail("its pixels end early");
		row.set(bytes.subarray(at, at + width * 4));
		return at + width * 4;
	}
	if (((bytes[at + 2] << 8) | bytes[at + 3]) !== width) fail("a scanline of the wrong width");
	at += 4;
	for (let channel = 0; channel < 4; channel++) {
		for (let x = 0; x < width;) {
			if (at >= bytes.length) fail("its runs end early");
			let count = bytes[at++];
			if (count > 128) {
				count -= 128;
				if (x + count > width) fail("a run past its scanline");
				const value = bytes[at++];
				for (let k = 0; k < count; k++) row[(x++) * 4 + channel] = value;
			}
			else {
				if (count === 0 || x + count > width || at + count > bytes.length) fail("a run past its scanline");
				for (let k = 0; k < count; k++) row[(x++) * 4 + channel] = bytes[at++];
			}
		}
	}
	return at;
}

export function decodeHdr(bytes) {
	const header = readHeader(bytes);
	const { width, height } = header;
	const data = new Float32Array(width * height * 4);
	const row = new Uint8Array(width * 4);
	let at = header.at;
	for (let y = 0; y < height; y++) {
		at = readScanline(bytes, at, row, width);
		const target = (header.bottomUp ? height - 1 - y : y) * width * 4;
		for (let x = 0; x < width; x++) {
			const e = row[x * 4 + 3];
			const scale = e ? Math.pow(2, e - 136) : 0;
			const o = target + x * 4;
			data[o] = row[x * 4] * scale;
			data[o + 1] = row[x * 4 + 1] * scale;
			data[o + 2] = row[x * 4 + 2] * scale;
			data[o + 3] = 1;
		}
	}
	return { width, height, data, hdr: true, format: "Radiance HDR" };
}
