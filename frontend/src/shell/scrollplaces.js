// scrollplaces.js - where the scrolled elements of a view are (docs/15 "Docking"): an element moved to a new parent
// loses its scroll place, and a hidden one reads 0, so the shell notes the places of the views shown before it moves
// their elements and gives each view its places back once it is shown again - a tab chosen again in its group, a pane
// gone back to on a phone. An element at its end stays at its end, so a log that grew while hidden shows its new lines.

export class ScrollPlaces {
	constructor() {
		this.places = new WeakMap();   // view element -> [{element, top, left, atEnd}]
	}

	// Notes the places in each of `views` (view elements) that is shown now; a hidden one keeps what was noted before.
	note(views) {
		for (const view of views) if (shown(view)) this.places.set(view, placesIn(view));
	}

	// Gives each of `views` that is shown now the places noted for it.
	restore(views) {
		for (const view of views) {
			const places = this.places.get(view);
			if (!places || !shown(view)) continue;
			for (const place of places) put(place);
			this.places.delete(view);
		}
	}
}

function shown(element) {
	return element.getClientRects().length > 0;
}

function placesIn(view) {
	return [view, ...view.querySelectorAll("*")].filter((e) => e.scrollTop || e.scrollLeft).map((e) => ({
		element: e, top: e.scrollTop, left: e.scrollLeft, atEnd: e.scrollTop > 0 && e.scrollHeight - e.scrollTop - e.clientHeight <= 1,
	}));
}

function put(place) {
	place.element.scrollTop = place.atEnd ? place.element.scrollHeight : place.top;
	place.element.scrollLeft = place.left;
}
