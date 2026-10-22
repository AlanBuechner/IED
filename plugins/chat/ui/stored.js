// stored.js - this device's chat choices (the chat shown, closed tabs, the details panel) in localStorage; a private
// window keeps them as long as the page.

export function stored(key, fallback) {
	try {
		const value = localStorage.getItem(key);
		return value === null ? fallback : JSON.parse(value);
	}
	catch {
		return fallback;
	}
}

export function store(key, value) {
	try {
		localStorage.setItem(key, JSON.stringify(value));
	}
	catch {
		// a private window: the choice lasts as long as the page
	}
}
