// format.js - numbers as the profiler shows them: durations with their unit, sizes, ruler times and tick steps.

const NS_PER_US = 1e3;
const NS_PER_MS = 1e6;
const NS_PER_S = 1e9;

function fixed(value, digits) {
	return value.toFixed(digits);
}

// "850 ns", "12.4 \u00b5s", "3.21 ms", "1.234 s".
export function duration(ns) {
	const sign = ns < 0 ? "-" : "";
	const v = Math.abs(ns);
	if (v < NS_PER_US) return sign + Math.round(v) + " ns";
	if (v < NS_PER_MS) return sign + fixed(v / NS_PER_US, v < 10 * NS_PER_US ? 2 : 1) + " \u00b5s";
	if (v < NS_PER_S) return sign + fixed(v / NS_PER_MS, v < 10 * NS_PER_MS ? 2 : (v < 100 * NS_PER_MS ? 1 : 0)) + " ms";
	return sign + fixed(v / NS_PER_S, 3) + " s";
}

// A point in time as precisely as a range of `spanNs` needs to tell its ends apart: "1.29 s" in a long range,
// "1290.05 ms" in a short one; to the microsecond without a range.
export function instant(ns, spanNs) {
	const step = spanNs > 0 ? Math.max(1, spanNs / 10) : NS_PER_US;
	const decimals = (unit) => Math.max(0, Math.min(6, Math.ceil(-Math.log10(step / unit))));
	return step >= NS_PER_MS ? fixed(ns / NS_PER_S, decimals(NS_PER_S)) + " s" : fixed(ns / NS_PER_MS, decimals(NS_PER_MS)) + " ms";
}

// Milliseconds with two decimals, the statistics table's unit.
export function ms(ns) {
	return fixed(ns / NS_PER_MS, ns < 10 * NS_PER_MS ? 2 : 1);
}

export function bytes(n) {
	if (n < 1024) return n + " B";
	if (n < 1048576) return fixed(n / 1024, 1) + " KiB";
	if (n < 1073741824) return fixed(n / 1048576, 1) + " MiB";
	return fixed(n / 1073741824, 2) + " GiB";
}

export function count(n) {
	return n.toLocaleString("en-US");
}

// The step between ruler ticks: the smallest 1, 2 or 5 times a power of ten that leaves at least `minPx` between ticks.
export function tickStep(spanNs, widthPx, minPx) {
	const least = spanNs * minPx / Math.max(1, widthPx);
	let step = 1;
	while (step * 10 <= least) step *= 10;
	for (const factor of [1, 2, 5, 10]) if (step * factor >= least) return step * factor;
	return step * 10;
}

// A ruler label at `ns` with as many decimals as the step needs: "74.250 s", "12.5 ms" when the span is short.
export function tickLabel(ns, step) {
	if (step >= NS_PER_S / 10 || ns >= 10 * NS_PER_S) {
		const decimals = Math.max(0, Math.min(9, Math.ceil(-Math.log10(step / NS_PER_S))));
		return fixed(ns / NS_PER_S, decimals) + " s";
	}
	if (step >= NS_PER_US) {
		const decimals = Math.max(0, Math.ceil(-Math.log10(step / NS_PER_MS)));
		return fixed(ns / NS_PER_MS, decimals) + " ms";
	}
	return fixed(ns / NS_PER_US, Math.max(0, Math.ceil(-Math.log10(step / NS_PER_US)))) + " \u00b5s";
}

// "2026-10-21 16:25" for a capture's start.
export function when(unixNs) {
	if (!unixNs) return "";
	const d = new Date(unixNs / NS_PER_MS);
	const pad = (n) => String(n).padStart(2, "0");
	return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) + " " + pad(d.getHours()) + ":" + pad(d.getMinutes());
}
