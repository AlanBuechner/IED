// newproject.test.js - the New Project dialog's choices: the templates a search and a language keep, the languages the
// filter offers, a project name's problem, and the folder a project goes in.

import { test, assertEqual } from "./harness.js";
import { filterTemplates, nameProblem, projectFolder, templateLanguages } from "../src/shell/newproject.js";

const TEMPLATES = [
	{ key: "ied.cpp/console", title: "Console App", description: "A C++ program that prints to the console", language: "cpp", languageName: "C++", tags: ["Console", "Windows"] },
	{ key: "ied.cpp/console-c", title: "Console App", description: "A C program", language: "c", languageName: "C", tags: ["Console"] },
	{ key: "ied.cpp/library", title: "Static Library", description: "A C++ static library", language: "cpp", languageName: "C++", tags: ["Library"] },
	{ key: "c4/module", title: "Module", description: "C4 code other projects compile in", language: "c4", languageName: "C4", tags: ["Library"] },
];

test("newproject: a search keeps the templates that hold every word, a language only its own", () => {
	const keys = (list) => list.map((t) => t.key);
	assertEqual(keys(filterTemplates(TEMPLATES, "", "")), keys(TEMPLATES), "nothing asked: all, in the server's order");
	assertEqual(keys(filterTemplates(TEMPLATES, "library", "")), ["ied.cpp/library", "c4/module"], "a tag");
	assertEqual(keys(filterTemplates(TEMPLATES, "console C++", "")), ["ied.cpp/console"], "every word, the language's name among them");
	assertEqual(keys(filterTemplates(TEMPLATES, "", "cpp")), ["ied.cpp/console", "ied.cpp/library"], "a language");
	assertEqual(keys(filterTemplates(TEMPLATES, "LIBRARY", "c4")), ["c4/module"], "in any case, with a language");
	assertEqual(filterTemplates(TEMPLATES, "rust", ""), []);
});

test("newproject: the languages, a name's problem, the project's folder", () => {
	assertEqual(templateLanguages(TEMPLATES), [{ id: "cpp", name: "C++" }, { id: "c", name: "C" }, { id: "c4", name: "C4" }]);
	assertEqual([nameProblem("Game"), nameProblem("my_game-2.0"), nameProblem("_tool")], ["", "", ""]);
	assertEqual([nameProblem("") !== "", nameProblem("bad name") !== "", nameProblem("-dash") !== "", nameProblem("a".repeat(65)) !== ""], [true, true, true, true]);
	assertEqual(projectFolder("C:\\GameDev\\", "Game"), "C:/GameDev/Game");
	assertEqual(projectFolder("/home/me/src", "Game"), "/home/me/src/Game");
	assertEqual([projectFolder("", "Game"), projectFolder("C:/x", "")], ["", ""]);
});
