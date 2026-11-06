// One thing on a board (docs/32 "Elements"): ink, a node shape, text, a note, a frame, an image, a free line or
// arrow, or a connector between two elements. Fields a kind does not use stay at their defaults and are not written.

include "Json.ch"
include "Geometry.ch"

namespace Boards
{
	enum class Kind { Stroke, Rect, Round, Ellipse, Diamond, Cylinder, Hexagon, Parallelogram, Text, Note, Frame, Line, Arrow, Connector, Image, Unknown }

	const double c_DefaultWidth = 2.0;
	const double c_DefaultFontSize = 16.0;

	Core::StringView KindName(Kind kind);
	Kind KindOf(Core::StringView name);
	// The shapes a diagram is made of.
	bool IsNode(Kind kind);
	// Placed by x, y, w, h: nodes, text, notes, frames and images.
	bool IsBoxed(Kind kind);
	// Drawn through points: ink, lines and arrows.
	bool IsPath(Kind kind);
	// The palette's names (docs/32): ink, gray and the hues; `allowNone` takes "none" too (a fill).
	bool IsColorName(Core::StringView name, bool allowNone);

	class Element
	{
		public Core::String m_Id;
		public Core::String m_Layer;
		public Kind m_Kind = Kind::Unknown;
		public double m_X;
		public double m_Y;
		public double m_W;
		public double m_H;
		public Core::Array<double> m_Points;
		public double m_Width = c_DefaultWidth;
		public double m_Opacity = 1.0;
		public double m_FontSize = c_DefaultFontSize;
		public Core::String m_Color;
		public Core::String m_Fill;
		public Core::String m_Style;
		public Core::String m_Text;
		public Core::String m_Key;
		public Core::String m_Desc;
		public Core::String m_By;
		public Core::String m_From;
		public Core::String m_To;
		public Core::String m_Start;
		public Core::String m_End;
		public Core::String m_Route;
		// A note's or text's subject: the element it annotates (layout moves it along, exports place it there).
		public Core::String m_About;
		// An image's file (workspace-relative), its size in pixels and the part of it shown (x, y, w, h in its
		// pixels; empty: all of it).
		public Core::String m_Src;
		public double m_ImageW;
		public double m_ImageH;
		public Core::Array<double> m_Crop;

		// x, y, w, h for a boxed element; its points' box for a path; empty for a connector (Board::Bounds has it).
		public Box Frame() const;
		public Core::StringView ColorOr(const char* fallback) const { return m_Color.IsEmpty() ? Core::StringView(fallback) : m_Color.View(); }
	}

	// The element as a JSON object, fields at their defaults left out.
	void WriteElement(JsonOut& json, const Element& element);
	// Sets the fields `node` has (an object of an element's fields, as a file or an op holds them) on `element`; a
	// null clears one. An op's own keys (op, ref, id, ids) are not fields. False with `problem` naming the first field
	// it refuses; `strict` refuses unknown keys too (an op's typo), a file's are passed over.
	bool ReadElementFields(const Serialization::DomNode& node, Element& element, bool strict, Core::String& problem);
	// What is wrong with a whole element (a missing field its kind needs), "" when nothing.
	Core::String ElementProblem(const Element& element);
}
