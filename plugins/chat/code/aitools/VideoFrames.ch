// What a recording does over time (video_view, docs/13 `media`): its frames are fed in order to a VideoAccumulator,
// which keeps per pixel how much the brightness changes from one frame to the next (flicker, noise) and how far it
// strays from its mean (pulsing, slow drift), and per grid cell the same with the cell's brightness frame by frame.
// The images a model is shown are made from it: a heat map of the change over the recording's mean frame, and a
// contact sheet of frames. Brightness is the sRGB bytes' luma, 0-255.
//
//   Ai::VideoAccumulator video;
//   for (each frame) video.Add(frame);       // Imaging::Rgba, all of one size
//   Ai::VideoAnalysis analysis = video.Finish();
//   Imaging::Rgba heat = Ai::ChangeHeatMap(analysis);

include "bedrock.ch"
include "math.ch"
include "Imaging/png.ch"

namespace Ai
{
	const int32 c_VideoGridColumns = 16;   // the cells the report ranks
	const int32 c_VideoGridRows = 9;

	struct VideoCell
	{
	public:
		public int32 m_X;
		public int32 m_Y;
		public int32 m_Width;
		public int32 m_Height;
		public double m_Change;   // mean |brightness - the frame before's| of its pixels
		public double m_Spread;   // mean of its pixels' deviation from their own mean over the recording
		public Core::Array<double> m_Brightness;   // the cell's mean brightness in each frame
	}

	struct VideoAnalysis
	{
	public:
		public int32 m_Width;
		public int32 m_Height;
		public int64 m_Frames;
		public Core::Array<double> m_ChangeByFrame;       // from frame 1 on: the mean |change| from the frame before
		public Core::Array<double> m_BrightnessByFrame;   // the mean brightness of each frame
		public Core::Array<float> m_Change;   // per pixel, rows top to bottom: the mean |change| between frames
		public Core::Array<float> m_Spread;   // per pixel: the standard deviation of its brightness over time
		public Core::Array<float> m_Mean;     // per pixel: its mean brightness
		public Core::Array<VideoCell> m_Cells;   // the grid, the most changing first
	}

	class VideoAccumulator
	{
	public:
		// Takes the next frame; false (and nothing kept) for one whose size is not the first frame's.
		public bool Add(const Imaging::Rgba& frame);
		public int64 Count() const { return m_Frames; }
		// What the frames added did; the accumulator is spent.
		public VideoAnalysis Finish();

	private:
		private int32 m_Width;
		private int32 m_Height;
		private int64 m_Frames;
		private Core::Array<float> m_Previous;
		private Core::Array<double> m_ChangeSum;
		private Core::Array<double> m_Sum;
		private Core::Array<double> m_SquareSum;
		private Core::Array<double> m_ChangeByFrame;
		private Core::Array<double> m_BrightnessByFrame;
		private Core::Array<Core::Array<double>> m_CellBrightness;   // by cell, a value a frame
	}

	// A frame's brightness (luma of the sRGB bytes, 0-255) at pixel i.
	float Brightness(const Imaging::Rgba& frame, int64 pixel);
	// `image` shrunk to `width` pixels across (its aspect kept), each pixel the mean of those it covers; as it is when
	// it is that narrow already.
	Imaging::Rgba Downscaled(const Imaging::Rgba& image, int32 width);
	// The pixels of `image` inside [x, x + width) x [y, y + height), clipped to it.
	Imaging::Rgba Cropped(const Imaging::Rgba& image, int32 x, int32 y, int32 width, int32 height);
	// The mean frame in grey, dimmed, under the change as heat (black, red, yellow, white): full heat at the change's
	// 99.5th percentile, or at `full` when that is larger (so a steady recording does not show its noise as heat).
	Imaging::Rgba ChangeHeatMap(const VideoAnalysis& analysis, float full);
	// The value `share` of the way up the sorted values (0.995: the 99.5th percentile); 0 for none.
	float Percentile(const Core::Array<float>& values, double share);
	// Frames side by side, `columns` to a row, each `cellWidth` across, a grey gap between them.
	Imaging::Rgba ContactSheet(const Core::Array<Imaging::Rgba>& frames, int32 columns, int32 cellWidth);
}
