// format.js - numbers as the GPU debugger shows them: sizes, GPU times, counts, capture times.

export function bytes(n) {
	if (!n && n !== 0) return "";
	if (n < 1024) return n + " B";
	if (n < 1048576) return (n / 1024).toFixed(1) + " KiB";
	if (n < 1073741824) return (n / 1048576).toFixed(1) + " MiB";
	return (n / 1073741824).toFixed(2) + " GiB";
}

// GPU milliseconds with enough digits for microsecond-scale events: "0.0052", "1.21", "10.6".
export function gpuMs(ms) {
	if (ms === undefined || ms === null) return "";
	if (ms < 0.01) return ms.toFixed(4);
	if (ms < 1) return ms.toFixed(3);
	if (ms < 10) return ms.toFixed(2);
	return ms.toFixed(1);
}

// "1.22M", "61.2K", "960".
export function count(n) {
	if (n === undefined || n === null) return "";
	if (n >= 1e6) return (n / 1e6).toFixed(2) + "M";
	if (n >= 1e4) return (n / 1e3).toFixed(1) + "K";
	return String(n);
}

// "1 submission", "3 submissions".
export function counted(n, noun) {
	return count(n) + " " + noun + (n === 1 ? "" : "s");
}

// A sidecar time "20261021-174337" as "Oct 21 17:43".
export function captureTime(stamp) {
	const m = /^(\d{4})(\d{2})(\d{2})-(\d{2})(\d{2})(\d{2})$/.exec(stamp || "");
	if (!m) return stamp || "";
	const date = new Date(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +m[6]);
	return date.toLocaleString("en-US", { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit", hour12: false });
}

// A float as a pixel value reads: NaN and the infinities by name, else 6 significant digits.
export function value(v) {
	if (typeof v === "string") return v;
	if (Number.isNaN(v)) return "NaN";
	if (!Number.isFinite(v)) return v > 0 ? "Inf" : "-Inf";
	return Number.isInteger(v) && Math.abs(v) < 1e7 ? String(v) : v.toPrecision(6).replace(/\.?0+$/, "");
}

// The separator between a value's parts, as the replayer writes it (a middle dot).
export const DOT = String.fromCharCode(0xb7);
