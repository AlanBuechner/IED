#include "Macro/MacroLayout.h"
#include <algorithm>

namespace C4Compiler
{
	namespace
	{
		uint32 LineBreaks(std::string_view file, uint32 begin, uint32 end)
		{
			return static_cast<uint32>(std::count(file.begin() + begin, file.begin() + end, '\n'));
		}
	}

	MacroExpansion MacroExpansion::Of(std::string macro, uint32 begin, uint32 end, const MacroTokens& tokens)
	{
		MacroExpansion expansion;
		expansion.macro = std::move(macro);
		expansion.begin = begin;
		expansion.end = end;
		expansion.pieces.reserve(tokens.size());
		for (const MacroToken& token : tokens)
		{
			if (token.spaceBefore && !expansion.text.empty()) expansion.text += ' ';
			const uint32 at = static_cast<uint32>(expansion.text.size());
			expansion.text += token.spelling;
			expansion.pieces.push_back({ at, static_cast<uint32>(expansion.text.size()), token.fileOffset });
		}
		return expansion;
	}

	const MacroExpansion::Piece* MacroExpansion::PieceAt(uint32 textOffset) const
	{
		auto after = std::upper_bound(pieces.begin(), pieces.end(), textOffset, [](uint32 offset, const Piece& piece) { return offset < piece.textBegin; });
		if (after == pieces.begin()) return nullptr;
		const Piece& piece = *(after - 1);
		return textOffset < piece.textEnd ? &piece : nullptr;
	}

	void MacroLayout::Skip(uint32 begin, uint32 end)
	{
		if (begin >= end) return;
		if (!m_Regions.empty() && m_Regions.back().expansion == 0 && m_Regions.back().end == begin)
		{
			m_Regions.back().end = end;
			return;
		}
		m_Regions.push_back({ begin, end, 0 });
	}

	uint32 MacroLayout::Expand(MacroExpansion expansion)
	{
		m_Regions.push_back({ expansion.begin, expansion.end, static_cast<uint32>(m_Expansions.size() + 1) });
		m_Expansions.push_back(std::move(expansion));
		return static_cast<uint32>(m_Expansions.size());
	}

	uint32 MacroLayout::ExpansionAt(uint32 fileOffset) const
	{
		auto after = std::upper_bound(m_Regions.begin(), m_Regions.end(), fileOffset, [](uint32 offset, const Region& region) { return offset < region.begin; });
		if (after == m_Regions.begin()) return 0;
		const Region& region = *(after - 1);
		return fileOffset < region.end ? region.expansion : 0;
	}

	uint32 MacroLayout::RenderedLength(std::string_view file, const Region& region) const
	{
		const uint32 breaks = LineBreaks(file, region.begin, region.end);
		return region.expansion == 0 ? breaks : static_cast<uint32>(Expansion(region.expansion).text.size()) + breaks;
	}

	std::string MacroLayout::Render(std::string_view file, uint32 begin, uint32 end) const
	{
		std::string out;
		out.reserve(end - begin);
		uint32 at = begin;
		for (const Region& region : m_Regions)
		{
			if (region.end <= at) continue;
			if (region.begin >= end) break;
			const uint32 from = std::max(region.begin, at);
			out.append(file.substr(at, from - at));
			const uint32 to = std::min(region.end, end);
			if (region.expansion != 0 && from == region.begin) out += Expansion(region.expansion).text;
			out.append(LineBreaks(file, from, to), '\n');
			at = to;
		}
		if (at < end) out.append(file.substr(at, end - at));
		return out;
	}

	uint32 MacroLayout::RenderedOffset(std::string_view file, uint32 fileOffset, uint32 expansion, uint32 inExpansion) const
	{
		int64 shift = 0;
		for (const Region& region : m_Regions)
		{
			if (expansion != 0 && region.expansion == expansion)
				return static_cast<uint32>(static_cast<int64>(region.begin) + shift) + inExpansion;
			if (region.begin >= fileOffset && expansion == 0) break;
			if (region.end > fileOffset && expansion == 0)
				return static_cast<uint32>(static_cast<int64>(region.begin) + shift); // inside a part left out: where it was
			shift += static_cast<int64>(RenderedLength(file, region)) - static_cast<int64>(region.end - region.begin);
		}
		return static_cast<uint32>(static_cast<int64>(fileOffset) + shift);
	}
}
