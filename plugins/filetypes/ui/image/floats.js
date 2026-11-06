// floats.js - the small float formats of textures as numbers: half (16-bit), Direct3D's 11- and 10-bit floats, and the
// shared-exponent R9G9B9E5.
//     halfToFloat(0x3c00) === 1

let halves = null;

function smallFloat(exponent, mantissa, mantissaBits) {
	if (exponent === 31) return mantissa ? NaN : Infinity;
	if (exponent === 0) return mantissa * Math.pow(2, -14 - mantissaBits);
	return (1 + mantissa / (1 << mantissaBits)) * Math.pow(2, exponent - 15);
}

export function halfToFloat(h) {
	if (!halves) {
		halves = new Float32Array(65536);
		for (let i = 0; i < 65536; i++) halves[i] = (i & 0x8000 ? -1 : 1) * smallFloat((i >> 10) & 31, i & 1023, 10);
	}
	return halves[h & 0xffff];
}

// R11G11B10_FLOAT's packed 32 bits as [r, g, b].
export function r11g11b10(v) {
	return [smallFloat((v >>> 6) & 31, v & 63, 6), smallFloat((v >>> 17) & 31, (v >>> 11) & 63, 6), smallFloat((v >>> 27) & 31, (v >>> 22) & 31, 5)];
}

// R9G9B9E5_SHAREDEXP's packed 32 bits as [r, g, b].
export function r9g9b9e5(v) {
	const scale = Math.pow(2, (v >>> 27) - 15 - 9);
	return [(v & 511) * scale, ((v >>> 9) & 511) * scale, ((v >>> 18) & 511) * scale];
}
