// DEFLATE's constants for the writer (RFC 1951): the window, the symbols' bases and extra bits, the tables that
// map a length or distance to its symbol, and the fixed codes.

namespace Compression
{
	const int64 kWindowSize = 32768;

	const int32 kHashBits = 15;

	const int32 kHashSize = 32768;

	const int32 kMinMatch = 3;

	const int32 kMaxMatch = 258;

	// The symbols a block gathers before its codes are chosen.
	const int32 kBlockSymbols = 16384;

	const int64 kMostStoredBytes = 65535;

	const int32 kLiteralCodes = 286;

	const int32 kDistanceCodes = 30;

	const int32 kEndOfBlockSymbol = 256;

	const int32 kMostCodeBits = 15;

	const int32 kMostLengthCodeBits = 7;

	const uint32 kMatchFlag = 0x80000000u;

	const uint16[29] kMatchLengthBase = { 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 13u, 15u, 17u, 19u, 23u, 27u, 31u, 35u, 43u, 51u, 59u, 67u, 83u, 99u, 115u, 131u, 163u, 195u, 227u, 258u };

	const uint8[29] kMatchLengthExtra = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 1u, 2u, 2u, 2u, 2u, 3u, 3u, 3u, 3u, 4u, 4u, 4u, 4u, 5u, 5u, 5u, 5u, 0u };

	const uint16[30] kMatchDistanceBase = { 1u, 2u, 3u, 4u, 5u, 7u, 9u, 13u, 17u, 25u, 33u, 49u, 65u, 97u, 129u, 193u, 257u, 385u, 513u, 769u, 1025u, 1537u, 2049u, 3073u, 4097u, 6145u, 8193u, 12289u, 16385u, 24577u };

	const uint8[30] kMatchDistanceExtra = { 0u, 0u, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 4u, 5u, 5u, 6u, 6u, 7u, 7u, 8u, 8u, 9u, 9u, 10u, 10u, 11u, 11u, 12u, 12u, 13u, 13u };

	// Each match length's symbol index, by length - 3; each distance's symbol, by distance - 1 up to 256 and by
	// 256 + ((distance - 1) >> 7) beyond (zlib's tables). Generated from the bases above.
	const uint8[256] kLengthIndex = {
		0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 8u, 9u, 9u, 10u, 10u, 11u, 11u, 12u, 12u, 12u, 12u, 13u, 13u, 13u, 13u,
		14u, 14u, 14u, 14u, 15u, 15u, 15u, 15u, 16u, 16u, 16u, 16u, 16u, 16u, 16u, 16u, 17u, 17u, 17u, 17u, 17u, 17u, 17u, 17u,
		18u, 18u, 18u, 18u, 18u, 18u, 18u, 18u, 19u, 19u, 19u, 19u, 19u, 19u, 19u, 19u, 20u, 20u, 20u, 20u, 20u, 20u, 20u, 20u,
		20u, 20u, 20u, 20u, 20u, 20u, 20u, 20u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u, 21u,
		22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 23u, 23u, 23u, 23u, 23u, 23u, 23u, 23u,
		23u, 23u, 23u, 23u, 23u, 23u, 23u, 23u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u,
		24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u,
		25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u,
		26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u,
		26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u,
		27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 28u
	};

	const uint8[512] kDistanceSymbol = {
		0u, 1u, 2u, 3u, 4u, 4u, 5u, 5u, 6u, 6u, 6u, 6u, 7u, 7u, 7u, 7u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u,
		9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u, 10u,
		11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u,
		12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u, 12u,
		13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u,
		13u, 13u, 13u, 13u, 13u, 13u, 13u, 13u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u,
		14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u,
		14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u, 14u,
		15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u,
		15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u,
		15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 15u, 0u, 0u, 16u, 17u, 18u, 18u, 19u, 19u,
		20u, 20u, 20u, 20u, 21u, 21u, 21u, 21u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 22u, 23u, 23u, 23u, 23u, 23u, 23u, 23u, 23u,
		24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 24u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u,
		25u, 25u, 25u, 25u, 25u, 25u, 25u, 25u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u,
		26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 26u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u,
		27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u, 27u,
		28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u,
		28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u,
		28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 28u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u,
		29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u,
		29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u,
		29u, 29u, 29u, 29u, 29u, 29u, 29u, 29u
	};

	// The fixed codes (RFC 1951 section 3.2.6), bit-reversed for writing least significant first.
	const uint8[286] kFixedLiteralLengths = {
		8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u,
		8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u,
		8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u,
		8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u,
		8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u,
		8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u,
		9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u,
		9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u,
		9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u,
		9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u,
		9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u,
		7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 8u, 8u, 8u, 8u, 8u, 8u
	};

	const uint16[286] kFixedLiteralCodes = {
		12u, 140u, 76u, 204u, 44u, 172u, 108u, 236u, 28u, 156u, 92u, 220u, 60u, 188u, 124u, 252u,
		2u, 130u, 66u, 194u, 34u, 162u, 98u, 226u, 18u, 146u, 82u, 210u, 50u, 178u, 114u, 242u,
		10u, 138u, 74u, 202u, 42u, 170u, 106u, 234u, 26u, 154u, 90u, 218u, 58u, 186u, 122u, 250u,
		6u, 134u, 70u, 198u, 38u, 166u, 102u, 230u, 22u, 150u, 86u, 214u, 54u, 182u, 118u, 246u,
		14u, 142u, 78u, 206u, 46u, 174u, 110u, 238u, 30u, 158u, 94u, 222u, 62u, 190u, 126u, 254u,
		1u, 129u, 65u, 193u, 33u, 161u, 97u, 225u, 17u, 145u, 81u, 209u, 49u, 177u, 113u, 241u,
		9u, 137u, 73u, 201u, 41u, 169u, 105u, 233u, 25u, 153u, 89u, 217u, 57u, 185u, 121u, 249u,
		5u, 133u, 69u, 197u, 37u, 165u, 101u, 229u, 21u, 149u, 85u, 213u, 53u, 181u, 117u, 245u,
		13u, 141u, 77u, 205u, 45u, 173u, 109u, 237u, 29u, 157u, 93u, 221u, 61u, 189u, 125u, 253u,
		19u, 275u, 147u, 403u, 83u, 339u, 211u, 467u, 51u, 307u, 179u, 435u, 115u, 371u, 243u, 499u,
		11u, 267u, 139u, 395u, 75u, 331u, 203u, 459u, 43u, 299u, 171u, 427u, 107u, 363u, 235u, 491u,
		27u, 283u, 155u, 411u, 91u, 347u, 219u, 475u, 59u, 315u, 187u, 443u, 123u, 379u, 251u, 507u,
		7u, 263u, 135u, 391u, 71u, 327u, 199u, 455u, 39u, 295u, 167u, 423u, 103u, 359u, 231u, 487u,
		23u, 279u, 151u, 407u, 87u, 343u, 215u, 471u, 55u, 311u, 183u, 439u, 119u, 375u, 247u, 503u,
		15u, 271u, 143u, 399u, 79u, 335u, 207u, 463u, 47u, 303u, 175u, 431u, 111u, 367u, 239u, 495u,
		31u, 287u, 159u, 415u, 95u, 351u, 223u, 479u, 63u, 319u, 191u, 447u, 127u, 383u, 255u, 511u,
		0u, 64u, 32u, 96u, 16u, 80u, 48u, 112u, 8u, 72u, 40u, 104u, 24u, 88u, 56u, 120u,
		4u, 68u, 36u, 100u, 20u, 84u, 52u, 116u, 3u, 131u, 67u, 195u, 35u, 163u
	};

	const uint8[30] kFixedDistanceLengths = {
		5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u,
		5u, 5u, 5u, 5u, 5u, 5u
	};

	const uint16[30] kFixedDistanceCodes = {
		0u, 16u, 8u, 24u, 4u, 20u, 12u, 28u, 2u, 18u, 10u, 26u, 6u, 22u, 14u, 30u,
		1u, 17u, 9u, 25u, 5u, 21u, 13u, 29u, 3u, 19u, 11u, 27u, 7u, 23u
	};

	const uint8[19] kLengthCodeOrder = { 16u, 17u, 18u, 0u, 8u, 7u, 9u, 6u, 10u, 5u, 11u, 4u, 12u, 3u, 13u, 2u, 14u, 1u, 15u };
}
