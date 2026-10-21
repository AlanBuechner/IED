#pragma once
#include "Core/Core.h"
#include "Json/JsonValue.h"
#include "Script/HostInterface.h"
#include "Script/ScriptCompiler.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace C4LSP::Server
{
	// Script folders the client names in initializationOptions.scripts: their files are scripts compiled against a
	// host's interface (docs/runtime/04), not part of the program the server analyzes (docs/tools/03 section 1).
	struct ScriptScope
	{
		std::vector<std::string> folders; // absolute, `/`-separated, without a trailing `/`
		std::string interfacePath;        // the host's .c4i
		std::vector<std::string> allow;   // the host headers its scripts may include
		std::map<std::string, std::string> defines;
	};

	// A host interface read for a scope's scripts, or why it did not read.
	struct LoadedInterface
	{
		Scripting::HostInterface host;
		std::unique_ptr<Scripting::HostIndex> index;
		std::string error;
	};

	// The scopes `scripts` lists ([{folders, interface, allow, defines}]); an entry without folders or an interface is
	// left out. Defines are NAME or NAME=VALUE, as after -D.
	std::vector<ScriptScope> ReadScriptScopes(const Json::Value& scripts);

	// A path as scopes compare paths: `/`-separated, no trailing `/`, lower case (Windows paths ignore case).
	std::string ComparablePath(std::string path);

	// The index of the scope whose folders hold the file at `path`, or -1.
	int32 ScopeOf(const std::vector<ScriptScope>& scopes, const std::string& path);

	// Host interfaces read once each, and again only when their file changes.
	class InterfaceCache
	{
	public:
		std::shared_ptr<const LoadedInterface> Get(const std::string& path);

	private:
		struct Entry
		{
			std::string stamp; // the file's size and time when it was read
			std::shared_ptr<const LoadedInterface> loaded;
		};
		std::map<std::string, Entry> m_Read;
	};
} // namespace C4LSP::Server
