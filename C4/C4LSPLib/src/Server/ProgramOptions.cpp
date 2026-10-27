#include "Server/ProgramOptions.h"
#include <filesystem>

namespace C4LSP::Server
{
	namespace fs = std::filesystem;

	namespace
	{
		fs::path PathOf(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

		std::vector<std::string> Strings(const Json::Value& list)
		{
			std::vector<std::string> strings;
			for (uint32 i = 0; i < list.Size(); ++i)
				if (list.At(i).IsString() && !list.At(i).GetString().empty()) strings.push_back(list.At(i).GetString());
			return strings;
		}

		std::map<std::string, std::string> Defines(const Json::Value& defines)
		{
			std::map<std::string, std::string> read;
			if (!defines.IsObject()) return read;
			for (const auto& [name, value] : defines.Raw().GetAsDescriptionMap())
				if (value.IsString()) read[std::string(name)] = std::string(Json::Value(value).GetString());
			return read;
		}
	}

	Scripting::CpuTarget ProgramOptions::Target() const
	{
		std::string error;
		std::optional<Scripting::CpuTarget> selected = Scripting::CpuTarget::FromFlags(targetFlags, error);
		return selected ? *selected : Scripting::CpuTarget();
	}

	bool ProgramOptions::operator==(const ProgramOptions& other) const
	{
		return name == other.name && includePaths == other.includePaths && defines == other.defines && targetFlags == other.targetFlags &&
			libraryFolder == other.libraryFolder && projectsFile == other.projectsFile && library == other.library;
	}

	ProgramOptions ReadProgramOptions(const std::string& name, const Json::Value& options)
	{
		ProgramOptions program;
		program.name = name;
		if (!options.IsObject()) return program;
		if (options.Member("projects").IsString()) program.projectsFile = options.Member("projects").GetString();
		const Json::Value home = options.Member("c4Home");
		if (home.IsString() && !home.GetString().empty()) program.libraryFolder = home.GetString() + "/Bedrock";
		program.includePaths = Strings(options.Member("includePaths"));
		program.defines = Defines(options.Member("defines"));
		program.targetFlags = Strings(options.Member("target"));
		return program;
	}

	std::vector<ProgramOptions> ReadPrograms(const Json::Value& programs)
	{
		std::vector<ProgramOptions> read;
		if (programs.IsObject())
			for (const auto& [name, options] : programs.Raw().GetAsDescriptionMap())
				read.push_back(ReadProgramOptions(std::string(name), Json::Value(options)));
		else
			for (uint32 i = 0; i < programs.Size(); ++i)
				read.push_back(ReadProgramOptions(std::to_string(i), programs.At(i)));
		return read;
	}

	ProgramOptions LibraryProgramOptions(const std::string& folder)
	{
		ProgramOptions program;
		program.name = "default library " + folder;
		program.library = true;
		program.libraryFolder = folder;
		const fs::path tests = PathOf(folder) / "Tests";
		std::error_code ec;
		if (fs::is_directory(tests, ec)) program.includePaths.push_back(tests.generic_string());
		return program;
	}

	std::string LibraryFolderAbove(const std::string& path)
	{
		std::error_code ec;
		for (fs::path folder = PathOf(path).parent_path(); !folder.empty() && folder != folder.parent_path(); folder = folder.parent_path())
			if (fs::is_regular_file(folder / "Prelude" / "prelude.ch", ec) && fs::is_regular_file(folder / "bedrock.ch", ec))
				return folder.lexically_normal().generic_string();
		return "";
	}
} // namespace C4LSP::Server
