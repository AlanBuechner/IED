// media.js - the media player ("media:<path>" tabs, F-EDT-95): a workspace's sound or video played by the browser from
// the server's byte ranges, so a long video seeks at once; speed, loop, and what it is (length, size, a video's
// resolution); a sound's waveform (sounds up to 64 MiB), where a click seeks. A file the browser cannot play says so,
// with a download and the hex view. It reloads when the file changes on disk, at the same place.

import { h, icon, replace } from "/ui-kit/dom.js";
import { fileUrl, fetchFile, fetchRange, watchFile } from "./fileurl.js";
import { downloadButton, formatSize, notice, openWithButton, saveFile, toolButton } from "./chrome.js";
import { extensionOf } from "./image/sources.js";
import { Waveform, peaksOf } from "./waveform.js";

const AUDIO = new Set(["mp3", "wav", "ogg", "oga", "opus", "flac", "m4a", "aac", "weba"]);
const WAVEFORM_MOST_BYTES = 64 * 1024 * 1024;
const SPEEDS = [0.25, 0.5, 0.75, 1, 1.25, 1.5, 2];

function formatTime(seconds) {
	if (!Number.isFinite(seconds)) return "--:--";
	const whole = Math.floor(seconds);
	const h = Math.floor(whole / 3600);
	const m = Math.floor((whole % 3600) / 60);
	const s = whole % 60;
	const tenths = Math.floor((seconds - whole) * 10);
	const mm = h ? String(m).padStart(2, "0") : String(m);
	return (h ? h + ":" : "") + mm + ":" + String(s).padStart(2, "0") + (seconds < 60 ? "." + tenths : "");
}

export class MediaPlayer {
	constructor(ctx, path) {
		this.ctx = ctx;
		this.path = path;
		this.ext = extensionOf(path);
		this.audio = AUDIO.has(this.ext);
		this.size = -1;
		this.info = h("span.ft-info");
		this.loopButton = toolButton("sync", "Loop", () => this.toggleLoop());
		this.speed = h("select.ft-select", { title: "Speed", on: { change: () => { this.element.playbackRate = Number(this.speed.value); } } },
			SPEEDS.map((s) => h("option", { value: String(s), selected: s === 1 ? "" : null }, s + "x")));
		this.element = h(this.audio ? "audio.ft-media" : "video.ft-media", { controls: "", preload: "metadata", playsinline: "" });
		this.wave = this.audio ? new Waveform(h("canvas.ft-wave"), (fraction) => this.seek(fraction)) : null;
		this.waveNote = h("div.ft-wave-note.muted");
		this.content = [this.audio ? h("div.ft-wave-box", null, this.wave.canvas, this.waveNote) : null, this.element];
		this.stage = h("div.ft-stage.ft-media-stage" + (this.audio ? ".audio" : ""), { tabindex: "-1" }, this.content);
		ctx.el.classList.add("ft-view");
		ctx.el.append(h("div.ft-tools", null, this.info, h("span.grow"), this.speed, this.loopButton, downloadButton(ctx, path)), this.stage);
		this.listen();
		this.resizer = new ResizeObserver(() => this.drawWave());
		this.resizer.observe(this.stage);
		this.stopWatch = watchFile(ctx.ied, ctx.wsId, path, (change) => (change === "deleted" ? this.problem("The file was deleted.") : this.reload()));
		this.reload();
	}

	listen() {
		const e = this.element;
		e.addEventListener("loadedmetadata", () => {
			if (!this.audio && !e.videoWidth) this.stage.classList.add("audio");
			this.describe();
		});
		e.addEventListener("timeupdate", () => this.drawWave());
		e.addEventListener("seeked", () => this.drawWave());
		e.addEventListener("error", () => {
			const code = e.error ? e.error.code : 0;
			this.problem(code === 4 ? "This browser cannot play " + this.path.split("/").pop() + ": its format or codec is not one it plays." : "The file could not be played (" + (e.error && e.error.message ? e.error.message : "media error " + code) + ").");
		});
		e.addEventListener("play", () => this.tick());
	}

	// The playhead moves smoothly while it plays (timeupdate comes four times a second).
	tick() {
		if (!this.wave || this.element.paused) return;
		this.drawWave();
		this.ticking = requestAnimationFrame(() => this.tick());
	}

	async reload() {
		const at = this.element.currentTime || 0;
		const playing = !this.element.paused;
		if (!this.element.isConnected) replace(this.stage, this.content);
		this.element.src = fileUrl(this.ctx.wsId, this.path);
		this.element.load();
		if (at) this.element.addEventListener("loadedmetadata", () => { this.element.currentTime = at; if (playing) this.element.play().catch(() => {}); }, { once: true });
		const generation = (this.generation = (this.generation || 0) + 1);
		try {
			this.size = (await fetchRange(fileUrl(this.ctx.wsId, this.path), 0, 0)).size;
			this.describe();
			if (this.wave) await this.loadWave(generation);
		}
		catch (e) {
			this.waveNote.textContent = e.message;
		}
	}

	async loadWave(generation) {
		if (this.size > WAVEFORM_MOST_BYTES) {
			this.waveNote.textContent = "No waveform for a sound over " + formatSize(WAVEFORM_MOST_BYTES) + ".";
			return;
		}
		this.waveNote.textContent = "Reading the waveform...";
		try {
			const peaks = await peaksOf(await fetchFile(fileUrl(this.ctx.wsId, this.path)));
			if (generation !== this.generation) return;
			this.wave.setPeaks(peaks);
			this.waveNote.textContent = "";
			this.peaks = peaks;
			this.describe();
			this.drawWave();
		}
		catch (e) {
			if (generation === this.generation) this.waveNote.textContent = "No waveform: this browser's Web Audio cannot decode the sound.";
		}
	}

	describe() {
		const e = this.element;
		const parts = [];
		if (Number.isFinite(e.duration)) parts.push(formatTime(e.duration));
		if (e.videoWidth) parts.push(e.videoWidth + " x " + e.videoHeight);
		if (this.peaks) parts.push(this.peaks.channels === 1 ? "mono" : this.peaks.channels === 2 ? "stereo" : this.peaks.channels + " channels", Math.round(this.peaks.sampleRate / 100) / 10 + " kHz");
		parts.push(this.ext.toUpperCase());
		if (this.size >= 0) parts.push(formatSize(this.size));
		replace(this.info, parts.map((p) => h("span.ft-chip", null, p)));
		this.info.title = this.path;
	}

	drawWave() {
		if (!this.wave) return;
		const duration = this.element.duration;
		this.wave.draw(Number.isFinite(duration) && duration > 0 ? this.element.currentTime / duration : 0);
	}

	seek(fraction) {
		const duration = this.element.duration;
		if (Number.isFinite(duration)) this.element.currentTime = fraction * duration;
		this.drawWave();
	}

	toggleLoop() {
		this.element.loop = !this.element.loop;
		this.loopButton.classList.toggle("on", this.element.loop);
	}

	problem(message) {
		this.element.removeAttribute("src");
		this.element.load();
		replace(this.stage, notice(message, [
			openWithButton(this.ctx, this.path, "hex", "Open in Hex Viewer"),
			h("button.btn", { on: { click: () => saveFile(this.ctx, this.path) } }, icon("cloud-download"), " Download"),
		], "Download it to play it in another program."));
	}

	focus() {
		this.element.focus({ preventScroll: true });
	}

	visible(shown) {
		if (shown) this.drawWave();
		else if (!this.element.paused && !this.audio) this.element.pause();
	}

	dispose() {
		this.generation = (this.generation || 0) + 1;
		cancelAnimationFrame(this.ticking);
		this.resizer.disconnect();
		this.stopWatch();
		this.element.pause();
		this.element.removeAttribute("src");
		this.element.load();
	}
}
