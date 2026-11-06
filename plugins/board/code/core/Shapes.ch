// The geometry of elements: a node's outline, where a connector runs between the elements it binds, how far a point
// is from an element, and an element's bounds with its stroke and arrow heads.

include "Element.ch"

namespace Boards
{
	// A boxed element's outline as a closed polygon (x, y pairs): its shape's own (an ellipse in 48 points, a
	// diamond's four corners), the box for text, notes, frames and the rest.
	Core::Array<double> Outline(const Element& element);
	// The center a connector aims at.
	Point CenterOf(const Element& element);
	// The route of a connector from `from` to `to`: the line between their centers clipped at both outlines, or with
	// `route` elbow an orthogonal path leaving and entering through the sides that face each other.
	Core::Array<double> ConnectorRoute(const Element& connector, const Element& from, const Element& to);
	// How long an arrow head is for a line of `width`.
	double HeadLength(double width);
	// The box an element covers with its route (a connector's, else empty), its line width and arrow heads.
	Box ElementBounds(const Element& element, const Core::Array<double>& route);
	// How far (px, py) is from what is drawn of the element: 0 inside a node, note, frame's title bar or text, else the
	// distance to its outline or line. `route` is a connector's.
	double HitDistance(const Element& element, const Core::Array<double>& route, double px, double py);
}
