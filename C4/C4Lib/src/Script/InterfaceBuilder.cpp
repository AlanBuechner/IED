#include "Script/InterfaceBuilder.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/SourceMacros.h"
#include "Compiler/Version.h"
#include "Model/ProgramModel.h"
#include "Script/InterfaceText.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <unordered_set>
#include <algorithm>
#include <set>

namespace Scripting
{
	namespace
	{
		std::string Normalized(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		}

		// The registered folder holding `absPath` (the deepest), or -1.
		int32 FolderOf(const std::vector<std::pair<std::string, uint32>>& folders, const std::string& absPath)
		{
			int32 best = -1;
			size_t bestLength = 0;
			for (size_t i = 0; i < folders.size(); ++i)
			{
				const std::string folder = Normalized(folders[i].first);
				const bool holds = absPath.size() > folder.size() && absPath.compare(0, folder.size(), folder) == 0 && absPath[folder.size()] == '/';
				if (holds && folder.size() >= bestLength) { best = static_cast<int32>(i); bestLength = folder.size(); }
			}
			return best;
		}

		std::vector<InterfaceProject> Projects(const SourceManager& sources)
		{
			const ProjectSet& set = sources.Projects();
			std::vector<InterfaceProject> projects(set.Count());
			for (uint32 p = 0; p < set.Count(); ++p)
			{
				projects[p].name = set.At(p).name;
				for (const std::string& depended : set.At(p).depends)
					for (uint32 q = 0; q < set.Count(); ++q)
						if (set.At(q).name == depended) projects[p].dependencies.push_back(q);
			}
			for (uint32 f = 0; f < sources.Folders().size(); ++f) projects[sources.Folders()[f].second].folders.push_back(f);
			return projects;
		}

		std::vector<MacroDefinition> Exported(const std::unordered_map<std::string, MacroTable>& exportedMacros, const std::string& absPath)
		{
			std::vector<MacroDefinition> macros;
			auto it = exportedMacros.find(absPath);
			if (it == exportedMacros.end()) return macros;
			for (const auto& [name, macro] : it->second.All()) macros.push_back(macro);
			return macros;
		}

		// Where a source sits as an interface file: its folder and the path in it, or the library's path.
		void Place(const SourceManager& sources, const SourceInfo& info, InterfaceFile& file)
		{
			file.library = info.library;
			file.path = info.displayName;
			if (info.library || info.absPath.empty()) return;
			const std::string absPath = Normalized(info.absPath);
			file.folder = FolderOf(sources.Folders(), absPath);
			if (file.folder >= 0) file.path = absPath.substr(Normalized(sources.Folders()[file.folder].first).size() + 1);
		}

		// The definitions of functions something else declares too: the interface keeps the declaration alone.
		std::unordered_set<const AstNode*> DeclaredElsewhere(const ProgramModel& model)
		{
			std::unordered_set<const AstNode*> definitions;
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const FunctionRecord* record : records)
			{
				if (!record->bodyNode || record->declarations.Count() < 2) continue;
				for (const AstNode* declaration : record->declarations)
					if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(declaration); fn && fn->GetBody().Get() == record->bodyNode)
						definitions.insert(fn);
			}
			return definitions;
		}

		// Two FNV-1a hashes of the interface with different starting points.
		std::array<uint8, 16> BuildIdOf(const HostInterface& host)
		{
			const std::string bytes = SerializeInterface(host);
			uint64 hashes[2] = { 14695981039346656037ull, 0x6c62272e07bb0142ull };
			for (uint64& hash : hashes)
				for (unsigned char c : bytes) { hash ^= c; hash *= 1099511628211ull; }
			std::array<uint8, 16> id{};
			for (int i = 0; i < 16; ++i) id[i] = static_cast<uint8>(hashes[i / 8] >> (8 * (i % 8)));
			return id;
		}
	}

	std::optional<HostInterface> BuildInterface(CompilerInstance& compiler, const std::unordered_map<std::string, MacroTable>& exportedMacros,
		HostBuild build, std::string* error)
	{
		const SourceManager& sources = compiler.GetSourceManager();
		const std::unordered_set<const AstNode*> declaredElsewhere = DeclaredElsewhere(compiler.GetProgramModel());
		HostInterface host;
		host.compilerVersion = CompilerVersion::Encoded;
		host.targetCpu = compiler.GetProgramModel().Target().Cpu();
		host.targetFeatures = compiler.GetProgramModel().Target().LlvmFeatures();
		host.projects = Projects(sources);
		host.rootProject = sources.Projects().Root();
		host.folderCount = static_cast<uint32>(sources.Folders().size());
		for (uint32 id = 1; id <= sources.Count(); ++id)
		{
			if (!sources.HasInfo(id)) continue;
			const SourceInfo& info = sources.GetInfo(id);
			Ref<CompilationUnit> unit = compiler.GetCompilationUnit(id);
			std::optional<std::string> text = unit && info.source ? InterfaceText(std::string(info.source->Data()), unit->GetOriginalChildren(), declaredElsewhere) : std::nullopt;
			if (!text)
			{
				if (error) *error = "cannot write the interface of " + info.displayName + ": its declarations do not fit its text";
				return std::nullopt;
			}
			InterfaceFile file;
			file.unitId = id;
			file.project = info.project;
			file.prelude = id == sources.PreludeId();
			Place(sources, info, file);
			file.text = std::move(*text);
			file.macros = Exported(exportedMacros, MacroIdentity(info));
			host.files.push_back(std::move(file));
		}
		host.symbols = std::move(build.symbols);
		host.reflectedTypes = std::move(build.reflectedTypes);
		host.inlineBodies = std::move(build.inlineBodies);
		host.buildId = BuildIdOf(host);
		return host;
	}

	std::vector<std::string> PreludeFunctionSymbols(const ProgramModel& model)
	{
		Core::Vector<const FunctionRecord*> records;
		model.AllFunctionRecords(records);
		std::vector<std::string> symbols;
		std::set<std::string> seen;
		for (const FunctionRecord* record : records)
			if (record->isBuiltin && record->isLinked && seen.insert(record->mangledName.c_str()).second) symbols.push_back(record->mangledName.c_str());
		return symbols;
	}
} // namespace Scripting
