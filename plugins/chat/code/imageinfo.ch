// What an image file says of itself, from its header (docs/13 T-MED-4, image_info): PNG, JPEG, GIF, WebP, BMP and DDS,
// the textures of a game among them - size, mip levels, pixel format, alpha, sRGB, a cube or an array.

include "bedrock.ch"

namespace Ai
{
	const int64 kImageInfoHeadBytes = 262144;   // how much of a file the header is looked for in (JPEG's can follow EXIF)

	class ImageInfo
	{
		public Core::String format;    // "PNG", "JPEG", "GIF", "WebP", "BMP", "DDS"
		public int64 width;
		public int64 height;
		public int64 mips;             // 1 for formats without mip levels
		public int64 layers;           // array size; 1 for one image
		public bool cube;
		public bool alpha;
		public bool srgb;
		public Core::String pixels;    // "8-bit RGBA", "BC7_UNORM_SRGB", ...

		// One line: "PNG, 1920x1080, 8-bit RGBA" or "DDS, 2048x2048, 12 mips, BC7_UNORM_SRGB (sRGB, alpha)".
		public Core::String Summary() const;
	}

	// The header of `bytes` (the start of a file); false with `problem` when it is no format this reads.
	bool ReadImageInfo(const Core::Array<uint8>& bytes, ImageInfo& info, Core::String& problem);
}
