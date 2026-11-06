// A picture of a board for the AI (docs/32 "Rendering for the AI"): the elements of the chosen layers inside a region,
// a coordinate grid labeled in world units in the margins, and each element's id (or a sketch group's name) on a tag
// beside it - so the model can name what it sees and place things where it means to.

include "Raster.ch"
include "Board.ch"

namespace Boards
{
	// The theme's colors (frontend/css/theme.css): the board's surface, the palette's names, the grid and the tags.
	class Theme
	{
		[operator("create")] public void Init();

		public Rgb Color(Core::StringView name) const;

		public Rgb m_Background;
		public Rgb m_Ink;
		public Rgb m_Grid;
		public Rgb m_GridText;
		public Rgb m_Tag;
		public Rgb m_TagBack;
		public Core::HashMap<Core::String, Rgb> m_Hues;
	}

	// The theme's colors read from the text of theme.css; names it lacks keep the grayscale defaults.
	Theme ReadTheme(Core::StringView css);

	// A label the picture shows over a box (a sketch group's name over its strokes).
	class ViewTag
	{
		public Core::String m_Label;
		public Box m_Box;
	}

	class ViewAsk
	{
		public Box m_Region;                                  // world units; empty: around everything drawn
		public Core::Array<Core::String> m_Layers;           // the layers drawn (ids); empty: the visible ones
		public bool m_Labels = true;
		public bool m_Grid = true;
		public int32 m_MaxSize = 1280;
		public Core::Array<ViewTag> m_Tags;
		public Core::HashMap<Core::String, bool> m_Untagged;  // elements a tag of m_Tags names already
	}

	class ViewResult
	{
		public Canvas m_Canvas;
		public double m_Scale;
		public Box m_Region;
		public int64 m_Drawn;
		public Core::Array<Core::String> m_Tagged;
		public Core::Array<Core::String> m_Missing;          // images whose file could not be read
	}

	alias ImageLoader = func<bool(Core::StringView, Imaging::Rgba&)>;

	ViewResult RenderBoard(const Board& board, const ViewAsk& ask, const Theme& theme, ImageLoader loadImage);
	// A rounded rectangle's outline as a polygon.
	Core::Array<double> RoundedOutline(double x, double y, double w, double h, double radius);
}
