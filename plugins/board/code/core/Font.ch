// Text as strokes in the Hershey simplex font (FontData.cf): how wide a line is at a font size, text wrapped to a
// width, and the pen strokes that draw it - what the server's renderer draws text with, and how the server sizes text
// it places. A line's box is 1.3 font sizes tall, its baseline 0.8 font sizes below its top.

include "Geometry.ch"

namespace Boards
{
	const double c_LineHeight = 1.3;

	double TextWidth(Core::StringView line, double size);
	// The box `text` takes, its lines split at line breaks.
	void MeasureText(Core::StringView text, double size, double& width, double& height);
	// `text`'s lines broken at spaces so none is wider than `width` (a word wider than it stands alone).
	Core::Array<Core::String> WrapText(Core::StringView text, double size, double width);
	// The strokes of one line of text whose box's top left is (x, y), each a polyline of x, y pairs.
	void LineStrokes(Core::StringView line, double x, double y, double size, Core::Array<Core::Array<double>>& strokes);
}
