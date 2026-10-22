// The codes of a block Deflate writes: Huffman code lengths from frequencies (limited to 15 bits), canonical codes,
// the fixed codes, and a dynamic block's header with its run-length coded lengths.

include "deflatetables.ch"
include "Container/array.ch"

namespace Compression
{
	int32 LengthIndex(int32 length);

	int32 DistanceSymbol(int32 distance);

	// Huffman code lengths for `count` symbols' frequencies (0: unused), none longer than `limit`: an optimal code,
	// its frequencies halved (each kept at least 1) until it fits.
	void BuildLengths(const uint32* frequencies, int32 count, int32 limit, uint8* lengths);

	// An optimal code's lengths (Huffman's two queues over the symbols sorted by weight): the longest.
	int32 OptimalLengths(const uint32* weights, int32 count, uint8* lengths);

	// Each symbol's canonical code (RFC 1951 section 3.2.2), bit-reversed for writing least significant first.
	void BuildCodes(const uint8* lengths, int32 count, uint16* codes);

	// A block's codes: literal/length and distance lengths and codes, and its code-length code when it is dynamic.
	struct BlockCodes
	{
		public uint8[286] literalLengths;
		public uint16[286] literalCodes;
		public uint8[30] distanceLengths;
		public uint16[30] distanceCodes;
	}

	// A block's codes wherever they are kept: the fixed codes are constants, a dynamic block's are built.
	struct CodeSet
	{
		public const uint8* literalLengths;
		public const uint16* literalCodes;
		public const uint8* distanceLengths;
		public const uint16* distanceCodes;
	}

	CodeSet FixedCodes();

	CodeSet CodesOf(const BlockCodes& block);

	// The symbols' frequencies, end of block included.
	struct Frequencies
	{
		public uint32[286] literals;
		public uint32[30] distances;
	}

	Frequencies BlockFrequencies(const Symbols& symbols);

	// The bits the symbols take under `codes`, extra bits included.
	int64 DataBits(const Frequencies& counts, const CodeSet& codes);

	// The code lengths run-length coded (16: the last length again, 17 and 18: zeros): a symbol and its extra bits each.
	struct LengthRuns
	{
		public Core::Array<uint8> symbols;
		public Core::Array<uint8> extras;
	}

	LengthRuns RunsOf(const uint8* lengths, int32 count);

	// A dynamic block's header, ready to write and to cost.
	struct DynamicHeader
	{
		public int32 literalCount;
		public int32 distanceCount;
		public int32 lengthCodeCount;
		public uint8[320] lengths;     // the literal then the distance code lengths
		public LengthRuns runs;
		public uint8[19] lengthCodeLengths;
		public uint16[19] lengthCodeCodes;
	}

	DynamicHeader HeaderFor(const BlockCodes& codes);

	int64 HeaderBits(const DynamicHeader& header);
}
