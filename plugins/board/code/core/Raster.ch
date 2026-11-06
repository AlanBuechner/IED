// A small software rasterizer for the server's pictures of a board (docs/32 "Rendering for the AI"): anti-aliased
// polylines of any width (dashed or not), filled polygons, text in the stroke font and scaled images, blended into an
// RGBA picture Bedrock's PNG writer encodes.

include "Font.ch"
include "Imaging/png.ch"

namespace Boards
{
	struct Rgb
	{
		public double m_R;
		public double m_G;
		public double m_B;
	}

	Rgb RgbOf(double r, double g, double b);
	// "#rrggbb" (or "#rgb"); false when it is not one.
	bool ParseHex(Core::StringView text, Rgb& color);

	class Canvas
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int32 width, int32 height, Rgb background);

		// Draws a polyline (x, y pairs in pixels) `width` wide; `dash` and `gap` (pixels, 0: solid) break it up.
		public void Stroke(const Core::Array<double>& points, double width, Rgb color, double alpha, double dash, double gap);
		// Fills a closed polygon (x, y pairs in pixels), its edges anti-aliased.
		public void Fill(const Core::Array<double>& polygon, Rgb color, double alpha);
		public void FillRect(double x, double y, double w, double h, Rgb color, double alpha);
		// One line of text whose box's top left is (x, y), `size` pixels tall.
		public void Text(Core::StringView line, double x, double y, double size, Rgb color, double alpha);
		// The part `crop` (x, y, w, h in the source's pixels) of `source` drawn into the box (x, y, w, h).
		public void Image(const Imaging::Rgba& source, double cx, double cy, double cw, double ch, double x, double y, double w, double h, double alpha);
		public void Encode(Core::Array<uint8>& png) const;
		// Mixes `color` into one pixel by `alpha` (outside the canvas: nothing).
		public void Blend(int32 x, int32 y, Rgb color, double alpha);

		public int32 m_Width;
		public int32 m_Height;
		public Imaging::Rgba m_Pixels;
	}
}
