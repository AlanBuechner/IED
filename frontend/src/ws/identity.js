// identity.js - who this tab is to the server (docs/10): a client id per tab (sessionStorage, so a reload is the same
// client), a device id per browser (localStorage), and the device class by width (docs/15).
// A duplicated tab inherits its source's sessionStorage, so a tab first asks the others whether its id is taken.

import { randomHex } from "./client.js";

const CLIENT_KEY = "ied.clientId";
const DEVICE_KEY = "ied.deviceId";
const CHANNEL = "ied.clients";
const ASK_FOR_MS = 60;

export function deviceClassOf(width) {
	if (width >= 1100) return "desktop";
	if (width >= 720) return "tablet";
	return "phone";
}

function stored(storage, key, make) {
	try {
		const value = storage.getItem(key);
		if (value) return value;
		const made = make();
		storage.setItem(key, made);
		return made;
	}
	catch {
		return make();
	}
}

// Resolves to {clientId, deviceId, deviceClass}; keeps answering other tabs that ask about this tab's id.
export async function resolveIdentity(win) {
	const w = win || window;
	const deviceId = stored(w.localStorage, DEVICE_KEY, () => "d-" + randomHex(16));
	let clientId = stored(w.sessionStorage, CLIENT_KEY, () => "c-" + randomHex(16));
	if (typeof BroadcastChannel === "function") {
		const channel = new BroadcastChannel(CHANNEL);
		if (await isTaken(channel, clientId)) {
			clientId = "c-" + randomHex(16);
			try {
				w.sessionStorage.setItem(CLIENT_KEY, clientId);
			}
			catch {
				// a private window without storage keeps the id in memory
			}
		}
		channel.onmessage = (event) => {
			if (event.data && event.data.ask === clientId) channel.postMessage({ taken: clientId });
		};
	}
	return { clientId, deviceId, deviceClass: deviceClassOf(w.innerWidth) };
}

function isTaken(channel, clientId) {
	return new Promise((resolve) => {
		const timer = setTimeout(() => done(false), ASK_FOR_MS);
		function done(taken) {
			clearTimeout(timer);
			channel.onmessage = null;
			resolve(taken);
		}
		channel.onmessage = (event) => {
			if (event.data && event.data.taken === clientId) done(true);
		};
		channel.postMessage({ ask: clientId });
	});
}
