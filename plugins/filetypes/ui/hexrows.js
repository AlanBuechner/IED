// hexrows.js - what the hex viewer writes: offsets as wide as the file needs, bytes and their characters, how many
// bytes a row holds in a width, an offset typed in hex or decimal, and the data inspector's values of the bytes at the
// selection (integers, floats, a character, UTF-8).
//     inspect(Uint8Array.of(0x00, 0x00, 0x80, 0x3f), true) -> [["Int8", "0"], ..., ["Float32", "1"], ...]

export const printable = (b) => b >= 0x20 && b < 0x7f;
export const byteText = (b) => b.toString(16).toUpperCase().padStart(2, "0");
export const charText = (b) => (printable(b) ? String.fromCharCode(b) : ".");

export function offsetDigits(size) {
	let digits = 8;
	while (digits < 16 && Math.pow(16, digits) < size) digits += 2;
	return digits;
}

export function offsetText(offset, digits) {
	return offset.toString(16).toUpperCase().padStart(digits, "0");
}

// The bytes a row holds when the view is `columns` characters wide: 16, 8 or 4, whichever fits (4 at least).
export function bytesPerRow(columns, digits) {
	for (const count of [16, 8]) {
		const width = digits + 2 + count * 3 + (count > 8 ? 1 : 0) + 2 + count;
		if (columns >= width) return count;
	}
	return 4;
}

// "0x1F", "1Fh" or "$1F" in hex, "31" in decimal; clamped into the file. Null for anything else.
export function parseOffset(text, size) {
	const t = String(text || "").trim();
	let value = NaN;
	if (/^(0x|\$)[0-9a-f]+$/i.test(t)) value = parseInt(t.replace(/^(0x|\$)/i, ""), 16);
	else if (/^[0-9a-f]+h$/i.test(t)) value = parseInt(t.slice(0, -1), 16);
	else if (/^\d+$/.test(t)) value = parseInt(t, 10);
	if (!Number.isFinite(value)) return null;
	return Math.max(0, Math.min(Math.max(0, size - 1), value));
}

function utf8At(bytes) {
	const b = bytes[0];
	const length = b < 0x80 ? 1 : b >= 0xf0 && b < 0xf8 ? 4 : b >= 0xe0 ? 3 : b >= 0xc0 ? 2 : 0;
	if (!length || bytes.length < length) return "-";
	try {
		const text = new TextDecoder("utf-8", { fatal: true }).decode(bytes.subarray(0, length));
		const code = text.codePointAt(0);
		return "U+" + code.toString(16).toUpperCase().padStart(4, "0") + (code >= 0x20 && code !== 0x7f ? " " + text : "");
	}
	catch (e) {
		return "-";
	}
}

// [label, value] rows for the bytes from the selection (`bytes`, up to 8 of them, fewer at the end of the file).
export function inspect(bytes, littleEndian) {
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const has = (n) => bytes.length >= n;
	const float = (v) => (Number.isInteger(v) ? String(v) : Number(v.toPrecision(9)).toString());
	return [
		["Binary", has(1) ? bytes[0].toString(2).padStart(8, "0") : "-"],
		["Int8", has(1) ? String(view.getInt8(0)) : "-"],
		["UInt8", has(1) ? String(bytes[0]) : "-"],
		["Int16", has(2) ? String(view.getInt16(0, littleEndian)) : "-"],
		["UInt16", has(2) ? String(view.getUint16(0, littleEndian)) : "-"],
		["Int32", has(4) ? String(view.getInt32(0, littleEndian)) : "-"],
		["UInt32", has(4) ? String(view.getUint32(0, littleEndian)) : "-"],
		["Int64", has(8) ? String(view.getBigInt64(0, littleEndian)) : "-"],
		["UInt64", has(8) ? String(view.getBigUint64(0, littleEndian)) : "-"],
		["Float32", has(4) ? float(view.getFloat32(0, littleEndian)) : "-"],
		["Float64", has(8) ? float(view.getFloat64(0, littleEndian)) : "-"],
		["UTF-8", has(1) ? utf8At(bytes) : "-"],
	];
}
