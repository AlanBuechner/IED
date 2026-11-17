// modelmenu.js - the chat's Model menu for other plugins (docs/04 "Which model a turn uses", docs/02 "Client bindings as
// built"): chat.modelMenu shows it at an element of theirs for a chat of theirs - or one they will make - and
// chat.modelLabel says what their button names, as a chat's own Model button does. The first caller is the editor's
// inline chat (Ctrl+I), whose box picks its chat's model and effort (owner, 2026-11-17: "we should still be able to
// change the model and effort from the default in the inline chat").

import { effortDefaultDetail, effortLabel } from "./chatmodel.js";
import { choiceLabel, choicesFrom, effortFallback, nextModel, showModelPicker, useOf } from "./modelpicker.js";

// What the Model menu lists for a workspace (modelpicker.js choicesFrom): its settings' models, the default chain as
// pacing has it now and the Claude models the CLI offers (aiusage; none when the server cannot say). Null without
// settings.
export async function modelChoices(ied, wsId) {
	const [settings, usage] = await Promise.all([ied.client.request("settings.get", { wsId }), ied.client.request("ai.usage", {}).catch(() => null)]);
	const chains = usage && usage.claude && usage.claude.chains ? usage.claude.chains : [];
	const uses = ied.contributions ? ied.contributions("aiUses").map((c) => c.value) : [];
	return settings && settings.values ? choicesFrom(settings.values, chains, usage && usage.claude ? usage.claude.models : [], uses) : null;
}

// A caller's choice ({use, model, effort, now, defaultEffort}) as the picker's `current`: `model` its own ("" its
// default), `now` the model its chat's next turn starts on as the server says ("" before a chat or its snapshot),
// `defaultEffort` its use's as the snapshot says (null before one).
export function currentOf(args) {
	const a = args || {};
	return { model: a.model || "", mode: "", effort: a.effort || "", now: a.now || "", use: a.use || "chat", defaultEffort: a.defaultEffort === undefined ? null : a.defaultEffort };
}

// What a Model button names: {model - the model the next turn starts on, by its label -, effort - the chat's, else its
// default and whose ("Medium (Inline chat)") -, title}; "..." while the models are not known.
export function labelOf(choices, current) {
	const name = nextModel(choices, current);
	const fallback = effortFallback(choices, current.use, current.defaultEffort, name);
	const use = useOf(choices, current.use);
	const whose = current.model ? "its own model" : use && use.model === name ? "the default for " + use.title + " (Settings > AI > Defaults)" : "the default chain's model now";
	return {
		model: choiceLabel(name, choices),
		effort: choices || current.effort ? effortLabel(current.effort, fallback) : "...",
		title: (name ? choiceLabel(name, choices) + ": " + whose + "; effort " + (current.effort ? "its own" : effortDefaultDetail(fallback)) + ". " : "")
			+ "Pick the model and effort this chat runs on",
	};
}

// chat.modelLabel {wsId, use, model?, effort?, now?, defaultEffort?} -> {model, effort, title}.
export async function modelLabel(ied, args) {
	return labelOf(await modelChoices(ied, (args && args.wsId) || ied.workspace.wsId), currentOf(args));
}

// chat.modelMenu {at, wsId, use, model?, effort?, now?, defaultEffort?, onChange} -> {close}: the Model menu at `at` (an
// element of the caller's); a model picked - {model}, "" its default - closes it and goes to onChange, an effort -
// {effort}, "" the default - too, and leaves it open (a phone shows a bottom sheet). The caller keeps the choice: on its
// chat (chat.set), or for the chat it will make (chat.create's model and effort).
export async function modelMenu(ied, args) {
	if (!args || !(args.at instanceof Element) || typeof args.onChange !== "function") throw new Error("chat.modelMenu needs {at, onChange}");
	const choices = await modelChoices(ied, args.wsId || ied.workspace.wsId);
	if (!choices) throw new Error("the settings could not be read");
	const close = showModelPicker(args.at, choices, currentOf(args), (change) => args.onChange(change), (effort) => args.onChange({ effort }));
	return { close };
}
