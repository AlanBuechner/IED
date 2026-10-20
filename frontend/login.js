// The login page (docs/11): asks /api/auth/state, posts the password to /api/auth/login, then opens the app.
"use strict";

const form = document.getElementById("form");
const password = document.getElementById("password");
const device = document.getElementById("device");
const submit = document.getElementById("submit");
const error = document.getElementById("error");
const hint = document.getElementById("hint");

function show(text) {
	error.textContent = text;
	error.hidden = !text;
}

function messageFor(status, body) {
	if (status === 401) return "That password is wrong.";
	if (status === 409) return "No owner password is set yet.";
	if (status === 429) return "Too many attempts. Try again in " + (body.retryAfterS || 60) + " s.";
	return "Sign-in failed (" + status + ").";
}

async function start() {
	const response = await fetch("/api/auth/state", { cache: "no-store" });
	const state = await response.json();
	if (state.authenticated) {
		location.replace("/");
		return;
	}
	if (!state.passwordSet) {
		hint.textContent = "No owner password is set yet. Sign in on the machine IED runs on, then set one in Settings > Remote Access.";
		form.hidden = true;
	}
}

async function signIn(event) {
	event.preventDefault();
	submit.disabled = true;
	show("");
	const request = { password: password.value };
	if (device.value.trim()) request.device = device.value.trim();
	const response = await fetch("/api/auth/login", {
		method: "POST",
		headers: { "Content-Type": "application/json" },
		body: JSON.stringify(request),
	});
	if (response.ok) {
		location.replace("/");
		return;
	}
	const body = await response.json().catch(() => ({}));
	show(messageFor(response.status, body));
	password.select();
	submit.disabled = false;
}

form.addEventListener("submit", signIn);
start().catch((e) => show("The server did not answer: " + e.message));
