#include "Interop/InteropSources.h"
#include "Compiler/SourceMacros.h"
#include <unordered_map>

namespace Scripting
{
	namespace
	{
		constexpr std::string_view kExtern = "extern";

		// Whether the file's text as its parse reads it may hold a directive: its own text, or what a use expands to.
		bool MayBind(const SourceInfo& info)
		{
			if (info.source->Text().find(kExtern) != std::string_view::npos) return true;
			if (!info.macros) return false;
			for (uint32 id = 1; id <= info.macros->ExpansionCount(); ++id)
				if (info.macros->Expansion(id).text.find(kExtern) != std::string::npos) return true;
			return false;
		}
	}

	std::vector<InteropFileInput> InteropInputsOf(const SourceManager& sources, bool projectFolders)
	{
		std::vector<InteropFileInput> inputs;
		std::unordered_map<uint32, std::vector<std::string>> reached; // each project's folders, found once
		for (uint32 id = 1; id <= sources.Count(); ++id)
		{
			if (!sources.HasInfo(id)) continue;
			const SourceInfo& info = sources.GetInfo(id);
			if (!info.source || info.binding || !MayBind(info)) continue;
			std::vector<std::string> folders;
			if (projectFolders)
			{
				auto [known, fresh] = reached.try_emplace(info.project);
				if (fresh) known->second = sources.FoldersReachedFrom(info.project);
				folders = known->second;
			}
			inputs.push_back({ info.displayName, MacroIdentity(info), sources.ExpandedText(id), std::move(folders) });
		}
		return inputs;
	}

	void RegisterBindings(SourceManager& sources, const std::vector<InteropFileResult>& files)
	{
		std::unordered_map<std::string, uint32> idOf;
		for (uint32 id = 1; id <= sources.Count(); ++id)
			if (sources.HasInfo(id)) idOf[MacroIdentity(sources.GetInfo(id))] = id;
		for (const InteropFileResult& file : files)
		{
			auto includer = idOf.find(file.absPath);
			if (includer == idOf.end()) continue;
			for (const BoundHeader& header : file.bindings)
				sources.RegisterBinding(header.path, header.displayName, header.text, includer->second);
		}
	}
}
