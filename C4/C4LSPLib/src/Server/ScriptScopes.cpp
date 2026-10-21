#include "Server/ScriptScopes.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace C4LSP::Server
{
	namespace fs = std::filesystem;

	namespace
	{
		std::vector<std::string> Strings(const Json::Value& list)
		{
			std::vector<std::string> strings;
			for (uint32 i = 0; i < list.Size(); ++i)
				if (list.At(i).IsString() && !list.At(i).GetString().empty()) strings.push_back(list.At(i).GetString());
			return strings;
		}

		std::map<std::string, std::string> Defines(const Json::Value& list)
		{
			std::map<std::string, std::string> defines;
			for (const std::string& define : Strings(list))
			{
				const size_t equals = define.find('=');
				defines[define.substr(0, equals)] = equals == std::string::npos ? "1" : define.substr(equals + 1);
			}
			return defines;
		}

		std::string FolderPath(std::string folder)
		{
			std::replace(folder.begin(), folder.end(), '\\', '/');
			while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
			return folder;
		}

		std::string StampOf(const std::string& path)
		{
			std::error_code ec;
			const fs::path file(std::u8string(path.begin(), path.end()));
			const auto size = fs::file_size(file, ec);
			if (ec) return "";
			const auto time = fs::last_write_time(file, ec).time_since_epoch().count();
			return ec ? "" : std::to_string(size) + "|" + std::to_string(time);
		}

		std::shared_ptr<const LoadedInterface> Read(const std::string& path)
		{
			auto loaded = std::make_shared<LoadedInterface>();
			if (Scripting::ReadInterface(path, loaded->host, &loaded->error))
				loaded->index = std::make_unique<Scripting::HostIndex>(loaded->host);
			else if (loaded->error.empty())
				loaded->error = "it does not read";
			return loaded;
		}
	}

	std::string ComparablePath(std::string path)
	{
		path = FolderPath(std::move(path));
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return path;
	}

	std::vector<ScriptScope> ReadScriptScopes(const Json::Value& scripts)
	{
		std::vector<ScriptScope> scopes;
		for (uint32 i = 0; i < scripts.Size(); ++i)
		{
			const Json::Value entry = scripts.At(i);
			ScriptScope scope;
			for (const std::string& folder : Strings(entry.Member("folders"))) scope.folders.push_back(FolderPath(folder));
			if (entry.Member("interface").IsString()) scope.interfacePath = entry.Member("interface").GetString();
			scope.allow = Strings(entry.Member("allow"));
			scope.defines = Defines(entry.Member("defines"));
			if (!scope.folders.empty() && !scope.interfacePath.empty()) scopes.push_back(std::move(scope));
		}
		return scopes;
	}

	int32 ScopeOf(const std::vector<ScriptScope>& scopes, const std::string& path)
	{
		const std::string file = ComparablePath(path);
		for (size_t i = 0; i < scopes.size(); ++i)
			for (const std::string& folder : scopes[i].folders)
			{
				const std::string prefix = ComparablePath(folder) + "/";
				if (file.compare(0, prefix.size(), prefix) == 0) return static_cast<int32>(i);
			}
		return -1;
	}

	std::shared_ptr<const LoadedInterface> InterfaceCache::Get(const std::string& path)
	{
		const std::string stamp = StampOf(path);
		auto known = m_Read.find(path);
		if (known != m_Read.end() && !stamp.empty() && known->second.stamp == stamp) return known->second.loaded;
		std::shared_ptr<const LoadedInterface> loaded = Read(path);
		m_Read[path] = { stamp, loaded };
		return loaded;
	}
} // namespace C4LSP::Server
