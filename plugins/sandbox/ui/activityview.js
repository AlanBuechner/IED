// activityview.js - the Sandbox activity's Activity window (docs/34 "The Sandbox activity"): what runs in the sandboxes
// and who started it - the AI (with the chat it works in) or you. A strip on top says what is running now; the runs
// are listed newest first; the console shows the run picked, else follows the newest one going (the AI's first), its
// output streaming as it comes. Filters keep to the AI's runs or yours. On a phone the list and the console take turns.

import { h, icon, replace } from "/ui-kit/dom.js";
import { aiRunning, followedRun, whoOf } from "./model.js";
import { Console, runHeader, runRow } from "./parts.js";

const FILTERS = [["all", "All"], ["ai", "AI"], ["you", "You"]];

export class ActivityView {
	constructor(ctx, store) {
		this.ctx = ctx;
		this.store = store;
		this.filter = "all";
		this.showingConsole = false;   // a phone shows the console instead of the list
		this.console = new Console();
		this.now = h("div.sbx-now");
		this.filters = h("div.sbx-filters", { role: "tablist" });
		this.list = h("div.sbx-runs", { role: "listbox", tabindex: "0", "aria-label": "Runs", on: { keydown: (event) => this.listKey(event) } });
		this.header = h("div.sbx-console-head");
		this.body = h("div.sbx-console", null, this.header, this.console.el);
		this.root = h("div.sbx.sbx-activity", null, this.now, h("div.sbx-body", null, h("div.sbx-runs-pane", null, this.filters, this.list), this.body));
		replace(ctx.el, this.root);
		this.stops = [store.on(() => this.render()), store.onOutput((runId, text) => this.console.append(runId, text))];
		this.timer = setInterval(() => this.tick(), 1000);
		this.render();
	}

	dispose() {
		for (const stop of this.stops) stop();
		clearInterval(this.timer);
	}

	visible(shown) {
		if (shown) this.render();
	}

	// Running times move on by themselves.
	tick() {
		if (this.store.runs().some((run) => run.running)) this.render();
	}

	shownRuns() {
		const runs = this.store.runs();
		if (this.filter === "ai") return runs.filter((run) => run.by === "ai");
		if (this.filter === "you") return runs.filter((run) => run.by !== "ai");
		return runs;
	}

	render() {
		const store = this.store;
		const runs = this.shownRuns();
		const run = followedRun(runs, store.shownRun);
		this.renderNow();
		replace(this.filters, FILTERS.map(([id, label]) => h("button.sbx-filter" + (this.filter === id ? ".on" : ""), { role: "tab", "aria-selected": this.filter === id ? "true" : "false", on: { click: () => { this.filter = id; this.render(); } } }, label)),
			h("span.sbx-spacer"),
			store.shownRun ? h("button.btn.flat.sbx-small", { title: "Follow the newest run again", on: { click: () => store.showRun(null) } }, icon("eye"), "Follow") : h("span.sbx-following.muted", { title: "The console shows the newest run as it starts" }, icon("eye"), "following"));
		if (!runs.length) {
			replace(this.list, h("div.sbx-empty.muted", null, this.filter === "ai" ? "The AI has run nothing in a sandbox yet. When it does - sandbox_run, sandbox_validate - it shows here as it happens." : "Nothing has run yet. Commands, builds and validations - the AI's and yours - show here as they run."));
		}
		else replace(this.list, runs.map((r) => runRow(r, store, run && r.id === run.id, () => {
			store.showRun(r.id);
			this.showingConsole = true;
			this.render();
		})));
		this.body.hidden = !run;
		if (run) {
			replace(this.header, h("button.ib.sbx-back", { title: "Back to the runs", "aria-label": "Back", on: { click: () => { this.showingConsole = false; this.render(); } } }, icon("arrow-left")), runHeader(run, store));
			this.console.show(run.id, store.outputs.get(run.id) || "");
		}
		this.root.classList.toggle("sbx-showing-details", this.showingConsole && !!run);
	}

	// What runs now, in one line: the AI's work first.
	renderNow() {
		const store = this.store;
		const going = store.runs().filter((run) => run.running);
		const ai = aiRunning(going);
		if (!going.length) {
			const last = store.runs()[0];
			replace(this.now, icon("vm"), h("span", null, store.ready() ? "Nothing is running in the sandboxes." : "Docker is not running."),
				last ? h("span.muted", null, " Last: " + whoOf(last, store.chatTitles).who + " ran " + (last.sandbox ? "in " + last.sandbox : "Docker")) : null);
			this.now.className = "sbx-now";
			return;
		}
		const lead = ai[0] || going[0];
		const who = whoOf(lead, store.chatTitles);
		replace(this.now, icon("loading", "spin"),
			h("span.sbx-now-who" + (who.ai ? ".ai" : ""), null, who.ai ? "The AI" + (who.chat ? " (\u201c" + who.chat + "\u201d)" : "") : who.who),
			h("span", null, " is running "), h("code", null, lead.label), h("span", null, " in " + (lead.sandbox || "Docker")),
			going.length > 1 ? h("span.muted", null, " - and " + (going.length - 1) + " more") : null);
		this.now.className = "sbx-now going" + (who.ai ? " ai" : "");
	}

	listKey(event) {
		const runs = this.shownRuns();
		const current = followedRun(runs, this.store.shownRun);
		const at = current ? runs.findIndex((run) => run.id === current.id) : -1;
		if (event.key === "ArrowDown" && at < runs.length - 1) this.store.showRun(runs[at + 1].id);
		else if (event.key === "ArrowUp" && at > 0) this.store.showRun(runs[at - 1].id);
		else return;
		event.preventDefault();
	}
}
