#include "Target/SourceBreakpoints.h"
#include <algorithm>
#include <cctype>

namespace C4DAP::Target
{
	namespace
	{
		// Windows paths are case-insensitive and take either slash; one spelling keys a source.
		std::string SourceKey(const std::string& path)
		{
			std::string key = path;
			for (char& c : key)
				c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return key;
		}

		const char* const kNotLoaded = "the program's code is not loaded yet";
		const char* const kNoCode = "no code on or after this line";
	}

	void SourceBreakpoints::Bind(Entry& entry, const Symbols& symbols, const Process& process, Patches& patches)
	{
		uint32_t bound = 0;
		for (uint64_t address : symbols.AddressesOfLine(entry.reported.source, entry.requested, bound))
			if (patches.Add(process, address)) entry.addresses.push_back(address);
		entry.reported.verified = !entry.addresses.empty();
		entry.reported.line = entry.reported.verified ? bound : entry.requested;
		entry.reported.message = entry.reported.verified ? "" : kNoCode;
	}

	std::vector<BoundBreakpoint> SourceBreakpoints::Set(const std::string& path, const std::vector<uint32_t>& lines, const Symbols* symbols,
		const Process& process, Patches& patches)
	{
		std::vector<Entry>& entries = m_BySource[SourceKey(path)];
		for (const Entry& old : entries)
			for (uint64_t address : old.addresses) patches.Release(process, address);
		entries.clear();
		std::vector<BoundBreakpoint> reported;
		for (uint32_t line : lines)
		{
			Entry entry;
			entry.requested = line;
			entry.reported = BoundBreakpoint{ m_NextId++, false, line, path, kNotLoaded };
			if (symbols) Bind(entry, *symbols, process, patches);
			reported.push_back(entry.reported);
			entries.push_back(std::move(entry));
		}
		return reported;
	}

	std::vector<BoundBreakpoint> SourceBreakpoints::BindPending(const Symbols& symbols, const Process& process, Patches& patches)
	{
		std::vector<BoundBreakpoint> tried;
		for (auto& [key, entries] : m_BySource)
			for (Entry& entry : entries)
				if (!entry.reported.verified)
				{
					Bind(entry, symbols, process, patches);
					tried.push_back(entry.reported);
				}
		return tried;
	}

	std::vector<uint32_t> SourceBreakpoints::IdsAt(uint64_t address) const
	{
		std::vector<uint32_t> ids;
		for (const auto& [key, entries] : m_BySource)
			for (const Entry& entry : entries)
				if (std::find(entry.addresses.begin(), entry.addresses.end(), address) != entry.addresses.end()) ids.push_back(entry.reported.id);
		return ids;
	}

	bool SourceBreakpoints::SetOn(uint64_t address, const SourcePosition& position) const
	{
		const auto source = m_BySource.find(SourceKey(position.path));
		if (source == m_BySource.end()) return false;
		for (const Entry& entry : source->second)
			if (entry.reported.line == position.line && std::find(entry.addresses.begin(), entry.addresses.end(), address) != entry.addresses.end())
				return true;
		return false;
	}
} // namespace C4DAP::Target
