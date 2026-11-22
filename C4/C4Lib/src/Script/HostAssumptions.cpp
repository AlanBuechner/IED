#include "Script/HostAssumptions.h"
#include <llvm/Support/BLAKE3.h>
#include <algorithm>
#include <string_view>

namespace C4Compiler
{
	namespace
	{
		bool Answered(const std::vector<std::pair<std::string, bool>>& asked, const std::unordered_set<std::string>& names)
		{
			for (const auto& [name, held] : asked)
				if ((names.count(name) != 0) != held) return false;
			return true;
		}

		void Feed(llvm::BLAKE3& hasher, std::string_view bytes)
		{
			hasher.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
		}

		std::string Hex(const llvm::BLAKE3Result<16>& sum)
		{
			static constexpr char kDigits[] = "0123456789abcdef";
			std::string hex;
			for (uint8_t byte : sum) { hex += kDigits[byte >> 4]; hex += kDigits[byte & 15]; }
			return hex;
		}

		std::string_view TrimmedEnd(std::string_view line)
		{
			while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
			return line;
		}
	}

	std::string InterfaceTextDigest(const std::string& text)
	{
		llvm::BLAKE3 lines;
		llvm::BLAKE3 assertions;
		uint32 number = 0;
		for (size_t at = 0; at <= text.size(); )
		{
			const size_t end = std::min(text.find('\n', at), text.size());
			const std::string_view line = TrimmedEnd(std::string_view(text).substr(at, end - at));
			++number;
			if (!line.empty())
			{
				Feed(lines, line);
				Feed(lines, "\n");
			}
			if (line.find("assert_") != std::string_view::npos) Feed(assertions, std::string_view(reinterpret_cast<const char*>(&number), sizeof(number)));
			at = end + 1;
		}
		Feed(lines, Hex(assertions.final<16>()));
		return Hex(lines.final<16>());
	}

	void InterfaceDigests::Make()
	{
		llvm::BLAKE3 whole;
		for (const InterfaceFile& file : m_Host->files)
		{
			m_Files.push_back(InterfaceTextDigest(file.text));
			Feed(whole, m_Files.back());
		}
		m_Whole = Hex(whole.final<16>());
	}

	const std::string& InterfaceDigests::Of(uint32 unitId)
	{
		std::call_once(m_Made, [this] { Make(); });
		static const std::string kNone;
		return unitId >= 1 && unitId <= m_Files.size() ? m_Files[unitId - 1] : kNone;
	}

	const std::string& InterfaceDigests::Whole()
	{
		std::call_once(m_Made, [this] { Make(); });
		return m_Whole;
	}

	bool AssumptionsHold(const HostAssumptions& assumptions, const HostIndex& index, const std::vector<std::string>& chunks, InterfaceDigests& digests)
	{
		if (!assumptions.wholeInterface.empty() && assumptions.wholeInterface != digests.Whole()) return false;
		if (!Answered(assumptions.symbols, index.symbols) || !Answered(assumptions.types, index.types)) return false;
		for (const auto& [name, digest] : assumptions.inlineBodies)
		{
			std::string error;
			if (index.inlineBodies->Digest(name, chunks, error) != digest || !error.empty()) return false;
		}
		return true;
	}
} // namespace C4Compiler
