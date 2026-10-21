// icons.js - the icons of files, folders, projects and solutions, for the shell and every plugin (`ied.icons`, docs/15
// "Icons"): the glyphs of shared/ui-kit/fileglyphs.js, labelled and colored as the plugins' `fileIcons` and
// `projectIcons` say. The shell knows no language; a workspace without language plugins shows plain pages.

import { iconFrom } from "/ui-kit/dom.js";
import { FOLDER, FOLDER_OPEN, SOLUTION, fileIconSpec, projectIconSpec, projectNeedsMarkers } from "/ui-kit/fileglyphs.js";

export class Icons {
	// `contributions(point)` gives a contribution point's entries as [{plugin: {id, base}, value}].
	constructor(contributions) {
		this.contributions = contributions;
		this.files = new Map();
	}

	// Forgets what was worked out: the plugins' contributions are read again.
	clear() {
		this.files.clear();
	}

	fileSpec(name) {
		const key = String(name || "").slice(String(name || "").lastIndexOf("/") + 1).toLowerCase();
		if (!this.files.has(key)) this.files.set(key, fileIconSpec(key, this.contributions("fileIcons")));
		return this.files.get(key);
	}

	file(name, extraClass) {
		return iconFrom(this.fileSpec(name), null, extraClass);
	}

	folder(open, extraClass) {
		return iconFrom(open ? FOLDER_OPEN : FOLDER, null, extraClass);
	}

	solution(extraClass) {
		return iconFrom(SOLUTION, null, extraClass);
	}

	projectSpec(project, rootNames) {
		return projectIconSpec(project, this.contributions("projectIcons"), rootNames);
	}

	project(project, rootNames, extraClass) {
		return iconFrom(this.projectSpec(project, rootNames), null, extraClass);
	}

	// Whether `project`'s icon depends on the files in its root folder (no language, preset or build system says).
	needsMarkers(project) {
		return projectNeedsMarkers(project, this.contributions("projectIcons"));
	}
}
