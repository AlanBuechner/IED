#include "Model/MemberSuggestion.h"
#include <algorithm>
#include <cctype>

namespace Scripting
{
	namespace
	{
		struct Rename { const char* written; const char* forms[3]; };

		// The C++ standard library's names and what Bedrock calls them, most likely first.
		const Rename kRenames[] = {
			{ "push_back", { "Push", "Append", nullptr } },
			{ "emplace_back", { "Push", "Append", nullptr } },
			{ "pop_back", { "Pop", nullptr, nullptr } },
			{ "size", { "Count", "ByteLength", "Length" } },
			{ "length", { "Count", "ByteLength", "Length" } },
			{ "empty", { "IsEmpty", nullptr, nullptr } },
			{ "c_str", { "CStr", "Data", nullptr } },
			{ "data", { "Data", "CStr", nullptr } },
			{ "substr", { "Substring", nullptr, nullptr } },
			{ "find", { "Find", "IndexOf", nullptr } },
			{ "insert", { "Insert", "Push", nullptr } },
			{ "emplace", { "Insert", "Push", nullptr } },
			{ "erase", { "Remove", "RemoveAt", nullptr } },
			{ "at", { "Get", "At", nullptr } },
			{ "front", { "First", "Front", nullptr } },
			{ "back", { "Last", "Back", nullptr } },
			{ "count", { "Count", "Contains", nullptr } },
		};

		bool Has(const std::vector<Core::String>& members, const char* name)
		{
			return std::find(members.begin(), members.end(), Core::String(name)) != members.end();
		}

		// Lower case without underscores: `starts_with` and `StartsWith` are the same name.
		std::string Folded(const Core::String& name)
		{
			std::string folded;
			for (char c : std::string(name.c_str()))
				if (c != '_') folded += (char)std::tolower((unsigned char)c);
			return folded;
		}

		// Edits (insert, delete, replace, swap two neighbours) from `a` to `b`.
		size_t Distance(const std::string& a, const std::string& b)
		{
			std::vector<std::vector<size_t>> d(a.size() + 1, std::vector<size_t>(b.size() + 1));
			for (size_t i = 0; i <= a.size(); ++i) d[i][0] = i;
			for (size_t j = 0; j <= b.size(); ++j) d[0][j] = j;
			for (size_t i = 1; i <= a.size(); ++i)
				for (size_t j = 1; j <= b.size(); ++j)
				{
					d[i][j] = std::min({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
					if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
				}
			return d[a.size()][b.size()];
		}

		Core::String Renamed(const Core::String& name, const std::vector<Core::String>& members)
		{
			for (const Rename& rename : kRenames)
				if (name == rename.written)
					for (const char* form : rename.forms)
						if (form && Has(members, form)) return form;
			return "";
		}

		Core::String Closest(const Core::String& name, const std::vector<Core::String>& members)
		{
			const std::string folded = Folded(name);
			const size_t allowed = folded.size() <= 4 ? 1 : 2;
			Core::String best;
			size_t bestDistance = allowed + 1;
			for (const Core::String& member : members)
			{
				const size_t distance = Distance(folded, Folded(member));
				if (distance < bestDistance) { best = member; bestDistance = distance; }
			}
			return best;
		}
	}

	Core::String SuggestedMember(const Core::String& name, const std::vector<Core::String>& members)
	{
		if (Core::String renamed = Renamed(name, members); !renamed.empty()) return renamed;
		return Closest(name, members);
	}
} // namespace Scripting
