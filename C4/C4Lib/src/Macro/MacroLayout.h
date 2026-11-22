#pragma once
#include "Core/Core.h"
#include "Macro/MacroToken.h"
#include <string>
#include <string_view>
#include <vector>

namespace C4Compiler
{
	/// One macro use the macro pass expanded (R8): the bytes [begin, end) of the file - the name, and a call's arguments - are
	/// read as `text` in their place. `text` is the expansion's tokens, a space where one came before; each piece is one of
	/// them, with where the file spells it (an argument's token) or not (a body's, a pasted or stringized one).
	struct MacroExpansion
	{
		struct Piece
		{
			uint32 textBegin = 0;
			uint32 textEnd = 0;
			uint32 fileOffset = MacroToken::kNotInFile;

			bool operator==(const Piece&) const = default;
		};

		uint32 begin = 0;
		uint32 end = 0;
		std::string macro;     // the name used
		std::string definedIn; // where its #define is (a file's name and line); empty for `__FILE__`, `__LINE__` and -D
		uint32 definedLine = 0;
		std::string text;
		std::vector<Piece> pieces;

		/// The expansion of `tokens` for the use [begin, end) of `macro`.
		static MacroExpansion Of(std::string macro, uint32 begin, uint32 end, const MacroTokens& tokens);
		/// The piece that holds `textOffset`; null between pieces.
		const Piece* PieceAt(uint32 textOffset) const;

		bool operator==(const MacroExpansion&) const = default;
	};

	/// What the macro pass did to one file, over its own text (R8): the bytes it leaves out (directive lines, the code of
	/// inactive `#if` groups) and the uses it expanded, in order. The lexer reads the file through it, so every token keeps
	/// the file's own position, and one from a macro knows its expansion; nothing rewrites the file's text.
	class MacroLayout : public Refable
	{
	public:
		/// A part of the file the lexer does not read as written: left out, or a use read as its expansion.
		struct Region
		{
			uint32 begin = 0;
			uint32 end = 0;
			uint32 expansion = 0; // 0: left out; else the expansion's id

			bool operator==(const Region&) const = default;
		};

		/// [begin, end) left out; one right after the region before it joins it.
		void Skip(uint32 begin, uint32 end);
		/// A use expanded; regions are added in the file's order. Returns its id (from 1).
		uint32 Expand(MacroExpansion expansion);

		bool Empty() const { return m_Regions.empty(); }
		const std::vector<Region>& Regions() const { return m_Regions; }
		uint32 ExpansionCount() const { return static_cast<uint32>(m_Expansions.size()); }
		/// The expansion `id` (from 1).
		const MacroExpansion& Expansion(uint32 id) const { return m_Expansions[id - 1]; }
		/// The id of the expansion whose use holds `fileOffset`; 0 when none does.
		uint32 ExpansionAt(uint32 fileOffset) const;

		/// [begin, end) of `file` as the macro pass leaves it: what it leaves out becomes its line breaks, and a use its
		/// expansion, then the line breaks the use spans - every line keeps its number. Render(file, 0, size) is the text a
		/// compile reads, for what needs it as text: a script host's interface file, the interop pass, a tool.
		std::string Render(std::string_view file, uint32 begin, uint32 end) const;
		/// Where a position is in Render(file, 0, size): `fileOffset` written, or `inExpansion` of the expansion `expansion`.
		uint32 RenderedOffset(std::string_view file, uint32 fileOffset, uint32 expansion, uint32 inExpansion) const;

		bool operator==(const MacroLayout& other) const { return m_Regions == other.m_Regions && m_Expansions == other.m_Expansions; }

	private:
		// The rendered length of `region` of `file`.
		uint32 RenderedLength(std::string_view file, const Region& region) const;

		std::vector<Region> m_Regions;
		std::vector<MacroExpansion> m_Expansions;
	};
}
