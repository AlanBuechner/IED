// PNG (ISO/IEC 15948) read into 8-bit RGBA and written from it. Reading takes every color type and bit depth, a palette
// and tRNS transparency, and Adam7 interlacing, and checks every chunk's CRC; 16-bit samples keep their high byte.
// Writing makes RGBA, or RGB when every pixel is opaque, each row with the filter that leaves the smallest sum (libpng's
// choice), at a Compression level.
//     Image::Rgba picture;
//     Image::ReadResult read = Image::DecodePng(bytes, count, picture);
//     Core::Array<uint8> png;
//     Image::EncodePng(picture, Compression::kDefaultLevel, png);

include "Compression/zlib.ch"

namespace Image
{
	// The most pixels an image read may have: 2^28, a gibibyte of RGBA.
	const int64 kMostPixels = 268435456;

	// Pixels four bytes each, red, green, blue and alpha, rows top to bottom with nothing between them.
	struct Rgba
	{
		public int32 width;
		public int32 height;
		public Core::Array<uint8> pixels;
	}

	// Whether a read worked, and why not when it did not.
	struct ReadResult
	{
		public bool ok;
		public const char* error;
	}

	// The image a PNG file's bytes hold, in `image` (its old pixels replaced).
	ReadResult DecodePng(const uint8* data, int64 size, Rgba& image);
	// `image` as a PNG file, appended to `out`.
	void EncodePng(const Rgba& image, int32 level, Core::Array<uint8>& out);
}
