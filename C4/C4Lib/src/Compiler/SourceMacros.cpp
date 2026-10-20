#include "Compiler/SourceMacros.h"

namespace Scripting
{
	std::string MacroIdentity(const SourceInfo& info)
	{
		return info.absPath.empty() ? "memory:" + std::to_string(info.id) : info.absPath;
	}

	std::vector<MacroPass::FileResult> ExpandSourceMacros(const SourceManager& sources, const std::map<std::string, std::string>& predefined,
		const std::unordered_map<uint32, MacroTable>& expanded)
	{
		std::vector<MacroPass::Input> files;
		std::unordered_map<std::string, uint32> idOf;
		for (uint32 id = 1; id <= sources.Count(); ++id)
		{
			if (!sources.HasInfo(id)) continue;
			const SourceInfo& info = sources.GetInfo(id);
			if (!info.source) continue;
			MacroPass::Input file;
			file.absPath = MacroIdentity(info);
			file.name = info.displayName;
			file.text = info.source->Data();
			file.predefined = predefined;
			if (!info.library)
				for (const auto& [name, value] : sources.Projects().At(info.project).defines) file.predefined[name] = value;
			if (auto interface = expanded.find(id); interface != expanded.end())
			{
				file.expanded = true;
				file.exports = interface->second;
			}
			idOf[file.absPath] = id;
			files.push_back(std::move(file));
		}
		auto resolve = [&](const std::string& including, const std::string& include) -> std::string
		{
			auto from = idOf.find(including);
			if (from == idOf.end()) return "";
			const IncludeTarget target = sources.ResolveInclude(include, from->second);
			return target.id ? MacroIdentity(sources.GetInfo(target.id)) : "";
		};
		return MacroPass::RunFiles(std::move(files), resolve);
	}
}
