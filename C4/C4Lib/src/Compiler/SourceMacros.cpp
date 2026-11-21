#include "Compiler/SourceMacros.h"

namespace Scripting
{
	namespace
	{
		MacroPass::Input InputOf(const SourceManager& sources, const SourceInfo& info, const std::map<std::string, std::string>& predefined)
		{
			MacroPass::Input file;
			file.absPath = MacroIdentity(info);
			file.name = info.displayName;
			file.text = info.source->Data();
			file.predefined = predefined;
			if (!info.library)
				for (const auto& [name, value] : sources.Projects().At(info.project).defines) file.predefined[name] = value;
			return file;
		}

		std::vector<MacroPass::FileResult> Run(const SourceManager& sources, std::vector<MacroPass::Input> files, std::unordered_map<std::string, uint32> idOf)
		{
			auto resolve = [&sources, idOf = std::move(idOf)](const std::string& including, const std::string& include) -> std::string
			{
				auto from = idOf.find(including);
				if (from == idOf.end()) return "";
				const IncludeTarget target = sources.ResolveInclude(include, from->second);
				return target.id ? MacroIdentity(sources.GetInfo(target.id)) : "";
			};
			return MacroPass::RunFiles(std::move(files), resolve);
		}
	}

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
			MacroPass::Input file = InputOf(sources, info, predefined);
			if (auto interface = expanded.find(id); interface != expanded.end())
			{
				file.expanded = true;
				file.exports = interface->second;
			}
			idOf[file.absPath] = id;
			files.push_back(std::move(file));
		}
		return Run(sources, std::move(files), std::move(idOf));
	}

	void ApplyMacroLayouts(SourceManager& sources, const std::vector<MacroPass::FileResult>& results)
	{
		std::unordered_map<std::string, const MacroPass::FileResult*> byIdentity;
		for (const MacroPass::FileResult& result : results) byIdentity[result.absPath] = &result;
		for (uint32 id = 1; id <= sources.Count(); ++id)
		{
			if (!sources.HasInfo(id)) continue;
			auto found = byIdentity.find(MacroIdentity(sources.GetInfo(id)));
			if (found != byIdentity.end()) sources.SetMacroLayout(id, found->second->layout);
		}
	}

	std::vector<MacroPass::FileResult> ExpandScriptMacros(const SourceManager& sources, const std::map<std::string, std::string>& predefined,
		const std::vector<uint32>& scriptUnits, const std::unordered_map<uint32, MacroTable>& exported)
	{
		std::vector<MacroPass::Input> files;
		std::unordered_map<std::string, uint32> idOf;
		for (uint32 id : scriptUnits)
		{
			MacroPass::Input file = InputOf(sources, sources.GetInfo(id), predefined);
			idOf[file.absPath] = id;
			files.push_back(std::move(file));
		}
		for (const auto& [id, exports] : exported)
		{
			MacroPass::Input file;
			file.absPath = MacroIdentity(sources.GetInfo(id));
			file.name = sources.GetInfo(id).displayName;
			file.expanded = true;
			file.exports = exports;
			idOf[file.absPath] = id;
			files.push_back(std::move(file));
		}
		return Run(sources, std::move(files), std::move(idOf));
	}
}
