#include "Script/ScriptSources.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/FileIo.h"
#include "Compiler/Projects.h"
#include <algorithm>
#include <filesystem>

namespace Scripting
{
	namespace fs = std::filesystem;

	namespace
	{
		std::string FolderPath(uint32 folder) { return std::string(kInterfaceRoot) + "/f" + std::to_string(folder); }
		std::string LibraryPath() { return std::string(kInterfaceRoot) + "/lib"; }
		std::string TextFolder() { return std::string(kInterfaceRoot) + "/script"; }

		std::string InterfacePath(const InterfaceFile& file)
		{
			if (file.library) return LibraryPath() + "/" + file.path;
			if (file.folder >= 0) return FolderPath(static_cast<uint32>(file.folder)) + "/" + file.path;
			return std::string(kInterfaceRoot) + "/text/" + file.path; // a host source the host was given as text
		}

		std::string Normalized(const fs::path& path)
		{
			std::string text = path.lexically_normal().string();
			std::replace(text.begin(), text.end(), '\\', '/');
			return text;
		}

		// "script", or "script<n>" when the host has a project of that name.
		std::string ScriptProjectName(const HostInterface& host)
		{
			auto taken = [&](const std::string& name) {
				return std::any_of(host.projects.begin(), host.projects.end(), [&](const InterfaceProject& p) { return p.name == name; });
			};
			std::string name = "script";
			for (uint32 n = 1; taken(name); ++n) name = "script" + std::to_string(n);
			return name;
		}

		// The projects name only the script's folders on disk as sources: the interface's folders and the script given
		// as text are in memory, where RegisterInterface and RegisterScriptSources add them (a project set asks the
		// file system about its sources, and "//c4i" is a network path to Windows).
		ProjectList Projects(const HostInterface& host, const std::vector<std::string>& scriptFolders, const std::map<std::string, std::string>& defines)
		{
			ProjectList list;
			for (const InterfaceProject& written : host.projects)
			{
				Project project;
				project.name = written.name;
				for (uint32 used : written.dependencies) project.depends.push_back(host.projects[used].name);
				list.projects.push_back(std::move(project));
			}
			Project script;
			script.name = ScriptProjectName(host);
			for (const std::string& folder : scriptFolders) script.sources.push_back(folder);
			script.defines = defines;
			script.depends.push_back(host.projects[host.rootProject].name);
			list.root = script.name;
			list.projects.push_back(std::move(script));
			return list;
		}

		MacroTable TableOf(const std::vector<MacroDefinition>& macros)
		{
			MacroTable table;
			for (const MacroDefinition& macro : macros)
			{
				if (macro.isFunctionLike) table.DefineFunction(macro.name, macro.params, macro.body);
				else table.Define(macro.name, macro.body);
			}
			return table;
		}

		// Through FileIo, so a script in a deep folder reads (BUG-356).
		bool ReadText(const std::string& path, std::string& text)
		{
			if (!IsFile(path)) return false;
			text = ReadFileBytes(path);
			return true;
		}

		bool Fail(std::string* error, const std::string& why)
		{
			if (error) *error = why;
			return false;
		}

		bool RegisterInterface(SourceManager& sources, const HostInterface& host, ScriptUnits& units, std::string* error)
		{
			for (uint32 p = 0; p < host.projects.size(); ++p)
				for (uint32 folder : host.projects[p].folders) sources.AddFolder(FolderPath(folder), p);
			uint32 prelude = 0;
			for (const InterfaceFile& file : host.files)
			{
				const uint32 id = sources.RegisterVirtual(InterfacePath(file), file.path, file.text, file.project, file.library);
				if (id != file.unitId)
					return Fail(error, "the interface's file '" + file.path + "' is unit " + std::to_string(file.unitId) + " of its host but registered as " +
						std::to_string(id) + " (a damaged interface)");
				if (file.prelude) prelude = id;
				if (file.folder < 0 && !file.library) sources.GetInfo(id).byName = true; // given as text: found by its name, as in the host
				sources.GetInfo(id).bindingsLine = file.bindingsLine;
				if (!file.macros.empty()) units.interfaceMacros[id] = TableOf(file.macros);
			}
			sources.SetLibrary(LibraryPath(), prelude);
			units.interfaceUnits = static_cast<uint32>(host.files.size());
			return true;
		}

		// The folder every one of `folders` is in: the script's own folder.
		std::string CommonFolder(const std::vector<std::string>& folders)
		{
			if (folders.empty()) return "";
			fs::path common(folders.front());
			for (const std::string& folder : folders)
				while (!common.empty() && Normalized(fs::path(folder).lexically_relative(common)).rfind("..", 0) == 0) common = common.parent_path();
			return Normalized(common);
		}

		// How diagnostics name a script file: by its path from the script's folder (`ui/menu.cf`), as its author knows
		// it; a file given as text outside that folder by the name it was given.
		std::string DisplayName(const ScriptFile& file, const std::string& absPath, const std::string& scriptFolder)
		{
			if (!scriptFolder.empty())
			{
				const std::string relative = Normalized(fs::path(absPath).lexically_relative(scriptFolder));
				if (!relative.empty() && relative != "." && relative.rfind("..", 0) != 0) return relative;
			}
			return file.text ? Normalized(file.path) : absPath;
		}

		bool RegisterScript(SourceManager& sources, const std::vector<ScriptFile>& files, const std::string& scriptFolder, ScriptUnits& units,
			std::string* error)
		{
			for (const ScriptFile& file : files)
			{
				std::string text;
				std::string absPath = Normalized(fs::path(TextFolder()) / file.path);
				if (file.text) text = *file.text;
				else if (!ReadText(file.path, text)) return Fail(error, "cannot read the script file " + file.path);
				else absPath = Normalized(fs::absolute(file.path));
				const uint32 id = sources.RegisterVirtual(absPath, DisplayName(file, absPath, scriptFolder), text, units.scriptProject, false);
				if (!id) return Fail(error, "the script names " + file.path + " twice");
				units.scriptUnits.push_back(id);
			}
			return true;
		}
	}

	std::optional<ScriptUnits> RegisterScriptSources(CompilerInstance& compiler, const HostInterface& host, const std::string& name,
		const std::vector<ScriptFile>& files, const std::map<std::string, std::string>& defines, std::string* error)
	{
		if (host.projects.empty() || host.rootProject >= host.projects.size())
		{
			Fail(error, "the interface describes no root project (a damaged interface)");
			return std::nullopt;
		}
		std::vector<std::string> scriptFolders;
		for (const ScriptFile& file : files)
			if (!file.text)
			{
				const std::string folder = Normalized(fs::absolute(file.path).parent_path());
				if (std::find(scriptFolders.begin(), scriptFolders.end(), folder) == scriptFolders.end()) scriptFolders.push_back(folder);
			}
		ProjectSetResult checked = ProjectSet::Check(Projects(host, scriptFolders, defines));
		if (!checked.set)
		{
			Fail(error, "the script '" + name + "' cannot be laid out over its host's projects: " + checked.error);
			return std::nullopt;
		}
		SourceManager& sources = compiler.GetSourceManager();
		sources.SetProjects(std::move(*checked.set));
		ScriptUnits units;
		units.scriptProject = static_cast<uint32>(host.projects.size());
		if (!RegisterInterface(sources, host, units, error)) return std::nullopt;
		for (const std::string& folder : scriptFolders) sources.AddFolder(folder, units.scriptProject);
		sources.AddFolder(TextFolder(), units.scriptProject);
		if (!RegisterScript(sources, files, CommonFolder(scriptFolders), units, error)) return std::nullopt;
		return units;
	}
} // namespace Scripting
