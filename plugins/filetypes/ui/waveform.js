// waveform.js - a sound's waveform: its peaks found once (decoded by Web Audio, every channel), drawn to the width of
// its canvas, the part played lighter and the playhead a line; a click or a tap seeks.
//     const wave = new Waveform(canvas, (fraction) => audio.currentTime = fraction * audio.duration);
//     wave.setPeaks(await peaksOf(bytes));
//     wave.draw(audio.currentTime / audio.duration);

const BUCKETS = 4096;

// [min, max] pairs of `BUCKETS` stretches of the sound, -1..1; null when Web Audio cannot decode it.
export async function peaksOf(bytes) {
	const context = new OfflineAudioContext(1, 1, 44100);
	const buffer = await context.decodeAudioData(bytes.slice().buffer);
	const peaks = new Float32Array(BUCKETS * 2);
	const length = buffer.length;
	for (let c = 0; c < buffer.numberOfChannels; c++) {
		const data = buffer.getChannelData(c);
		for (let b = 0; b < BUCKETS; b++) {
			const from = Math.floor((b * length) / BUCKETS);
			const to = Math.max(from + 1, Math.floor(((b + 1) * length) / BUCKETS));
			let low = peaks[b * 2];
			let high = peaks[b * 2 + 1];
			for (let i = from; i < to && i < length; i++) {
				const v = data[i];
				if (v < low) low = v;
				if (v > high) high = v;
			}
			peaks[b * 2] = low;
			peaks[b * 2 + 1] = high;
		}
	}
	return { peaks, duration: buffer.duration, channels: buffer.numberOfChannels, sampleRate: buffer.sampleRate };
}

export class Waveform {
	constructor(canvas, seek) {
		this.canvas = canvas;
		this.peaks = null;
		canvas.addEventListener("pointerdown", (event) => {
			if (!this.peaks) return;
			const box = canvas.getBoundingClientRect();
			seek(Math.min(1, Math.max(0, (event.clientX - box.left) / box.width)));
		});
	}

	setPeaks(peaks) {
		this.peaks = peaks;
	}

	draw(played) {
		const canvas = this.canvas;
		const ratio = window.devicePixelRatio || 1;
		const width = Math.max(1, Math.round(canvas.clientWidth * ratio));
		const height = Math.max(1, Math.round(canvas.clientHeight * ratio));
		if (canvas.width !== width || canvas.height !== height) {
			canvas.width = width;
			canvas.height = height;
		}
		const g = canvas.getContext("2d");
		const style = getComputedStyle(canvas);
		g.clearRect(0, 0, width, height);
		const middle = height / 2;
		g.fillStyle = style.getPropertyValue("--border-1");
		g.fillRect(0, Math.floor(middle), width, Math.max(1, Math.round(ratio)));
		if (!this.peaks) return;
		const peaks = this.peaks.peaks;
		const buckets = peaks.length / 2;
		const playedX = Math.round((played || 0) * width);
		const unplayed = style.getPropertyValue("--text-3");
		const done = style.getPropertyValue("--text-1");
		for (let x = 0; x < width; x++) {
			const from = Math.floor((x * buckets) / width);
			const to = Math.max(from + 1, Math.floor(((x + 1) * buckets) / width));
			let low = 0;
			let high = 0;
			for (let b = from; b < to; b++) {
				low = Math.min(low, peaks[b * 2]);
				high = Math.max(high, peaks[b * 2 + 1]);
			}
			g.fillStyle = x < playedX ? done : unplayed;
			const top = middle - high * middle * 0.95;
			g.fillRect(x, top, 1, Math.max(1, (high - low) * middle * 0.95));
		}
		g.fillStyle = style.getPropertyValue("--text-0");
		g.fillRect(Math.min(width - Math.round(ratio), playedX), 0, Math.max(1, Math.round(ratio)), height);
	}
}
