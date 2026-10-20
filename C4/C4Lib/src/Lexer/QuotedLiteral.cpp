#include "Lexer/LiteralScanner.h"

namespace Scripting
{
	namespace
	{
		constexpr uint32 kMaxCodePoint = 0x10FFFF;

		bool IsSurrogate(uint32 cp) { return cp >= 0xD800 && cp <= 0xDFFF; }
		bool IsOctalDigit(char c) { return c >= '0' && c <= '7'; }

		int HexValue(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		uint8 UnitWidthOf(std::string_view prefix)
		{
			if (prefix == "u") return 2;
			if (prefix == "U") return 4;
			return 1;
		}

		// The code units of a literal as they are decoded, and how many characters they came from.
		class LiteralBuilder
		{
		public:
			explicit LiteralBuilder(uint8 unitWidth) { m_Facts.unitWidth = unitWidth; }

			uint8 UnitWidth() const { return m_Facts.unitWidth; }
			uint32 MaxUnit() const { return m_Facts.unitWidth == 4 ? 0xFFFFFFFFu : (1u << (8 * m_Facts.unitWidth)) - 1; }

			// A raw unit (`\x41`, `\101`): stored as written, one character.
			void Unit(uint32 value)
			{
				Append(value);
				Count(value, true);
			}

			// A code point (a source character, `\u00e9`): encoded in the literal's width, one character.
			void CodePoint(uint32 cp)
			{
				if (UnitWidth() == 1) AppendUtf8(cp);
				else if (UnitWidth() == 2 && cp > 0xFFFF)
				{
					Append(0xD800 + ((cp - 0x10000) >> 10));
					Append(0xDC00 + ((cp - 0x10000) & 0x3FF));
				}
				else Append(cp);
				Count(cp, FitsOneUnit(cp));
			}

			// Source bytes copied as they are (a char8 literal keeps the file's UTF-8).
			void Bytes(std::string_view bytes, uint32 cp)
			{
				m_Units.append(bytes.data(), bytes.size());
				Count(cp, bytes.size() == 1);
			}

			void Note(LiteralIssue issue, uint32 detail = 0)
			{
				if (m_Facts.issue != LiteralIssue::None) return;
				m_Facts.issue = issue;
				m_Facts.detail = detail;
			}

			bool FitsOneUnit(uint32 cp) const { return UnitWidth() == 4 || (UnitWidth() == 2 ? cp <= 0xFFFF : cp <= 0x7F); }
			uint32 Characters() const { return m_Characters; }
			uint32 LastCharacter() const { return m_Last; }
			bool LastFitsOneUnit() const { return m_LastFits; }
			Core::String TakeUnits() { return std::move(m_Units); }
			const LiteralFacts& Facts() const { return m_Facts; }

		private:
			void Append(uint32 value)
			{
				for (uint8 b = 0; b < UnitWidth(); ++b)
					m_Units += static_cast<char>((value >> (8 * b)) & 0xFF);
			}

			void AppendUtf8(uint32 cp)
			{
				if (cp < 0x80) Append(cp);
				else if (cp < 0x800) { Append(0xC0 | (cp >> 6)); Append(0x80 | (cp & 0x3F)); }
				else if (cp < 0x10000) { Append(0xE0 | (cp >> 12)); Append(0x80 | ((cp >> 6) & 0x3F)); Append(0x80 | (cp & 0x3F)); }
				else { Append(0xF0 | (cp >> 18)); Append(0x80 | ((cp >> 12) & 0x3F)); Append(0x80 | ((cp >> 6) & 0x3F)); Append(0x80 | (cp & 0x3F)); }
			}

			void Count(uint32 value, bool fits) { ++m_Characters; m_Last = value; m_LastFits = fits; }

			Core::String m_Units;
			LiteralFacts m_Facts;
			uint32 m_Characters = 0;
			uint32 m_Last = 0;
			bool m_LastFits = true;
		};

		// The UTF-8 sequence starting at `text[i]`: its code point and length, or length 0 when it is not UTF-8.
		struct Utf8Character { uint32 codePoint = 0; size_t length = 0; };

		Utf8Character DecodeUtf8(std::string_view text, size_t i)
		{
			const auto byte = [&](size_t k) { return static_cast<unsigned char>(text[k]); };
			const unsigned char lead = byte(i);
			const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
			if (length == 0 || i + length > text.size()) return {};
			uint32 cp = length == 1 ? lead : lead & (0xFF >> (length + 1));
			for (size_t k = 1; k < length; ++k)
			{
				if ((byte(i + k) & 0xC0) != 0x80) return {};
				cp = (cp << 6) | (byte(i + k) & 0x3F);
			}
			const uint32 smallest[] = { 0, 0, 0x80, 0x800, 0x10000 };
			if (cp < smallest[length] || cp > kMaxCodePoint || IsSurrogate(cp)) return {};
			return { cp, length };
		}

		size_t CopySourceCharacter(std::string_view text, size_t i, LiteralBuilder& literal)
		{
			const Utf8Character character = DecodeUtf8(text, i);
			if (character.length == 0)
			{
				if (literal.UnitWidth() == 1) literal.Bytes(text.substr(i, 1), static_cast<unsigned char>(text[i]));
				else literal.Note(LiteralIssue::InvalidUtf8);
				return i + 1;
			}
			if (literal.UnitWidth() == 1) literal.Bytes(text.substr(i, character.length), character.codePoint);
			else literal.CodePoint(character.codePoint);
			return i + character.length;
		}

		// `\xHH`: at most two hex digits per byte of the code unit.
		size_t HexEscape(std::string_view text, size_t i, LiteralBuilder& literal)
		{
			const size_t maxDigits = 2u * literal.UnitWidth();
			uint32 value = 0;
			size_t digits = 0;
			while (digits < maxDigits && i < text.size() && HexValue(text[i]) >= 0)
			{
				value = value * 16 + static_cast<uint32>(HexValue(text[i]));
				++i;
				++digits;
			}
			if (digits == 0) literal.Note(LiteralIssue::UnknownEscape, 'x');
			literal.Unit(value);
			return i;
		}

		// `\nnn`: one to three octal digits; `\0` is NUL.
		size_t OctalEscape(std::string_view text, size_t i, LiteralBuilder& literal)
		{
			uint32 value = 0;
			for (size_t digits = 0; digits < 3 && i < text.size() && IsOctalDigit(text[i]); ++digits, ++i)
				value = value * 8 + static_cast<uint32>(text[i] - '0');
			if (value > literal.MaxUnit()) literal.Note(LiteralIssue::EscapeOutOfRange, value);
			literal.Unit(value & literal.MaxUnit());
			return i;
		}

		// `\uXXXX` / `\UXXXXXXXX`: exactly 4 / 8 hex digits naming a code point that is not a surrogate.
		size_t UniversalEscape(std::string_view text, size_t i, char kind, LiteralBuilder& literal)
		{
			const size_t digits = kind == 'u' ? 4 : 8;
			uint32 cp = 0;
			for (size_t k = 0; k < digits; ++k)
			{
				const int v = i + k < text.size() ? HexValue(text[i + k]) : -1;
				if (v < 0)
				{
					literal.Note(LiteralIssue::BadUniversalEscape, static_cast<uint8>(kind));
					return i + k;
				}
				cp = cp * 16 + static_cast<uint32>(v);
			}
			if (cp > kMaxCodePoint || IsSurrogate(cp))
				literal.Note(LiteralIssue::BadUniversalEscape, static_cast<uint8>(kind));
			else
				literal.CodePoint(cp);
			return i + digits;
		}

		char SimpleEscape(char c)
		{
			switch (c)
			{
			case 'n': return '\n';
			case 't': return '\t';
			case 'r': return '\r';
			case 'a': return '\a';
			case 'b': return '\b';
			case 'f': return '\f';
			case 'v': return '\v';
			case 'e': return '\x1B';
			case '\\': return '\\';
			case '"': return '"';
			case '\'': return '\'';
			case '?': return '?';
			default: return 0;
			}
		}

		// `i` is at the backslash.
		size_t Escape(std::string_view text, size_t i, LiteralBuilder& literal)
		{
			if (i + 1 >= text.size() || text[i + 1] == '\n')
			{
				literal.Note(LiteralIssue::UnknownEscape, '\n');
				return i + 1;
			}
			const char c = text[i + 1];
			if (c == 'x') return HexEscape(text, i + 2, literal);
			if (IsOctalDigit(c)) return OctalEscape(text, i + 1, literal);
			if (c == 'u' || c == 'U') return UniversalEscape(text, i + 2, c, literal);
			if (const char simple = SimpleEscape(c))
			{
				literal.Unit(static_cast<unsigned char>(simple));
				return i + 2;
			}
			literal.Note(LiteralIssue::UnknownEscape, static_cast<unsigned char>(c));
			return i + 2;
		}

		void CheckOneCharacter(LiteralBuilder& literal)
		{
			if (literal.Characters() == 0) literal.Note(LiteralIssue::EmptyCharacter);
			else if (literal.Characters() > 1) literal.Note(LiteralIssue::MultiCharacter);
			else if (!literal.LastFitsOneUnit()) literal.Note(LiteralIssue::CharacterOutOfRange, literal.LastCharacter());
		}
	}

	size_t QuotedLiteralPrefix(std::string_view rest)
	{
		const auto quoteAt = [&](size_t i) { return i < rest.size() && (rest[i] == '"' || rest[i] == '\''); };
		if (quoteAt(0)) return 0;
		if (rest.size() > 2 && rest[0] == 'u' && rest[1] == '8' && quoteAt(2)) return 2;
		if (!rest.empty() && (rest[0] == 'u' || rest[0] == 'U') && quoteAt(1)) return 1;
		return std::string_view::npos;
	}

	ScannedLiteral ScanQuoted(std::string_view rest, size_t prefixLength)
	{
		const char quote = rest[prefixLength];
		LiteralBuilder literal(UnitWidthOf(rest.substr(0, prefixLength)));
		size_t i = prefixLength + 1;
		while (true)
		{
			if (i >= rest.size() || rest[i] == '\n' || rest[i] == '\r')
			{
				literal.Note(LiteralIssue::Unterminated, static_cast<uint8>(quote));
				break;
			}
			if (rest[i] == quote)
			{
				++i;
				break;
			}
			i = rest[i] == '\\' ? Escape(rest, i, literal) : CopySourceCharacter(rest, i, literal);
		}
		if (quote == '\'')
			CheckOneCharacter(literal);

		ScannedLiteral scanned;
		scanned.type = quote == '"' ? TokenType::Text : TokenType::Char;
		scanned.length = i;
		scanned.facts = literal.Facts();
		scanned.text = literal.TakeUnits();
		return scanned;
	}
}
