// chatprivacy.test.js - a chat's privacy in the chat plugin's views (docs/04 "Three settings, each its own"): the
// solution's settings read from settings:<wsId>, the Privacy menu's rows and the button's words, what chat.set is sent
// when one is flipped, the composer's Privacy button, and the Model menu greying cloud models for a chat without them.

import { test, assertEqual, assertTrue } from "./harness.js";
import { ChatModel, privacyInEffect, privacyLabel, privacyRows, privacyToggled, solutionPrivacy } from "/plugins/ied.chat/ui/chatmodel.js";
import { Composer } from "/plugins/ied.chat/ui/composer.js";
import { choicesFrom, showModelPicker } from "/plugins/ied.chat/ui/modelpicker.js";

test("chat privacy: the solution's settings, the menu's rows and the button's words", () => {
	const solution = solutionPrivacy({ "ai.privacy.noWeb": { value: true }, "ai.privacy.hiddenChats": { value: false }, "ai.models": { value: {} } });
	assertEqual(solution, { noCloudModels: false, hidden: false, noWeb: true });
	assertEqual(solutionPrivacy(null), { noCloudModels: false, hidden: false, noWeb: false });
	assertEqual(privacyLabel({}, {}), "None");
	assertEqual(privacyLabel({ noCloudModels: true }, solution), "Local only, No web");
	assertEqual(privacyInEffect({ hidden: true }, solution), { noCloudModels: false, hidden: true, noWeb: true });
	const rows = privacyRows({ hidden: true }, solution);
	assertEqual(rows.map((r) => [r.value, r.enabled, r.icon, r.keys]), [["noCloudModels", true, null, ""], ["hidden", true, "$(check)", ""], ["noWeb", false, "$(check)", "solution"]]);
	assertEqual(privacyToggled({ hidden: true }, "noCloudModels"), { privacy: { noCloudModels: true, hidden: true } });
	assertEqual(privacyToggled({ hidden: true, noWeb: true }, "hidden"), { privacy: { noWeb: true } });
});

test("chat privacy: the composer's Privacy button flips one setting and keeps the others", () => {
	const sets = [];
	const composer = new Composer({ send: async () => true, stop: () => {}, unqueue: async () => null, sendQueued: () => {}, set: (change) => sets.push(change), choices: async () => null,
		mentionRows: async () => [], upload: async () => null, fileUrl: (path) => path, toast: () => {} });
	const model = new ChatModel();
	model.snapshot({ chatId: "c1", wsId: "w1", title: "Taxes", mode: "agent", permission: "auto", state: "idle", privacy: { noCloudModels: true }, turns: [] });
	composer.solutionPrivacy = { hidden: true };
	composer.update(model);
	document.body.append(composer.el);
	const button = composer.el.querySelector(".chat-privacy-dd");
	assertTrue(button !== null, "the Privacy button");
	assertEqual(button.textContent, "Local only, Hidden");
	assertTrue(button.querySelector(".ci") !== null && /the solution's/.test(button.title), button.title);
	button.click();
	const items = [...document.querySelectorAll(".menu-item")];
	assertEqual(items.map((el) => el.textContent.trim()), ["No cloud models", "Hidden from other chatssolution", "No web"]);
	assertTrue(items[1].classList.contains("dis"), "the solution's row cannot be turned off");
	items[2].click();
	assertEqual(sets, [{ privacy: { noCloudModels: true, noWeb: true } }]);
	composer.el.remove();
});

test("chat privacy: the Model menu greys cloud models for a chat with none", () => {
	const values = { "ai.models": { value: { qwen: { endpoint: "http://127.0.0.1:8080/v1" }, router: { endpoint: "http://models.example.com/v1", privacy: "cloud" }, "claude-opus": { provider: "claude-code", model: "opus" } } } };
	const choices = choicesFrom(values, [], null, []);
	assertEqual(choices.models.map((m) => [m.name, m.cloud]), [["qwen", false], ["router", true], ["claude-opus", true]]);
	const at = document.createElement("button");
	document.body.append(at);
	const picked = [];
	const close = showModelPicker(at, choices, { model: "", mode: "agent", effort: "", now: "qwen", use: "chat", defaultEffort: "", noCloudModels: true }, (change) => picked.push(change), () => {});
	const rows = [...document.querySelectorAll(".chat-picker-row:not([data-default])")];
	const disabled = rows.filter((r) => r.disabled).map((r) => r.querySelector(".name").textContent);
	assertTrue(disabled.includes("router") && !disabled.includes("qwen"), "cloud rows are greyed: " + disabled.join(", "));
	assertTrue(rows.some((r) => r.disabled && /cloud: not for this chat/.test(r.textContent)), "and say why");
	rows.find((r) => r.disabled).click();
	assertEqual(picked, []);
	close();
	at.remove();
});
