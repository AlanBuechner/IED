// index.js - the git plugin's UI module (docs/05 "git", mockup 05): the Git activity, the status bar's branch with what
// is to push and pull (from the workspace's first repository), and the commands that open the activity and fetch,
// pull, push, branch, stash and commit in it.

import { h, icon, replace } from "/ui-kit/dom.js";
import { GitActivityView } from "./activity.js";
import { RepoModel, syncText } from "./model.js";

const views = new Set();

function last() {
	return [...views].pop() || null;
}

// Runs `fn` on the shown Git activity, opening it first.
async function inActivity(ied, fn) {
	await ied.commands.run("shell.showActivity", { id: "ied.git" });
	if (last()) return fn(last());
}

export function activate(ied) {
	if (!document.querySelector("link[href='" + ied.plugin.base + "ui/git.css']")) document.head.append(h("link", { rel: "stylesheet", href: ied.plugin.base + "ui/git.css" }));
	ied.commands.handle("ied.git.show", () => ied.commands.run("shell.showActivity", { id: "ied.git" }));
	ied.commands.handle("ied.git.fetch", () => inActivity(ied, (view) => view.fetch()));
	ied.commands.handle("ied.git.pull", () => inActivity(ied, (view) => view.pull()));
	ied.commands.handle("ied.git.push", () => inActivity(ied, (view) => view.push()));
	ied.commands.handle("ied.git.newBranch", () => inActivity(ied, (view) => view.newBranch(null)));
	ied.commands.handle("ied.git.stash", () => inActivity(ied, (view) => view.stash()));
	ied.commands.handle("ied.git.commit", () => inActivity(ied, (view) => {
		view.select("local");
		view.changes.message.focus();
	}));
}

export function GitActivity(ctx) {
	const view = new GitActivityView(ctx);
	views.add(view);
	return { dispose: () => {
		views.delete(view);
		view.dispose();
	} };
}

// "main ^3 v1": the branch (or the detached commit) and what is to push and pull; null without a repository.
export function gitStatusText(head) {
	if (!head) return null;
	const name = head.detached ? head.oid.slice(0, 7) : head.branch || "no commits yet";
	return { name, ahead: head.ahead || 0, behind: head.behind || 0, title: syncText(head) };
}

export function GitStatus(ctx) {
	const model = new RepoModel();
	let stopRepo = null;
	let repoId = null;
	const render = () => {
		const status = gitStatusText(model.head);
		ctx.el.hidden = !status;
		if (!status) return;
		ctx.el.title = status.title || status.name;
		replace(ctx.el, icon("git-branch"), h("span", null, status.name),
			status.ahead ? [icon("arrow-up", "sm"), String(status.ahead)] : null,
			status.behind ? [icon("arrow-down", "sm"), String(status.behind)] : null);
	};
	const follow = (repo) => {
		if (repo && repo.repoId === repoId) return;
		if (stopRepo) stopRepo();
		stopRepo = null;
		repoId = repo ? repo.repoId : null;
		model.head = null;
		if (repo) stopRepo = ctx.ied.client.subscribe("git:" + repo.repoId, {
			snap: (data) => {
				model.snapshot(data);
				render();
			},
			evt: (kind, data) => {
				model.event(kind, data);
				render();
			},
		});
		render();
	};
	const stopRepos = ctx.ied.client.subscribe("gitrepos:" + ctx.wsId, {
		snap: (data) => follow((data.repos || [])[0]),
		evt: (kind, data) => follow((data.repos || [])[0]),
	});
	render();
	return { dispose: () => {
		stopRepos();
		if (stopRepo) stopRepo();
	} };
}
