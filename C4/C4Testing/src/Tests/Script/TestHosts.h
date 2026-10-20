#pragma once
#include "Script/HostInterface.h"
#include "Script/ScriptCompiler.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Testing
{
	/// A host built in this process, as `C4.exe --script-host` builds one, for compiling scripts against: its
	/// interface and its index. Empty interface (no files) when the host did not compile.
	struct TestHost
	{
		Scripting::HostInterface interface;
		std::unique_ptr<Scripting::HostIndex> index;
		std::string diagnostics;
	};

	/// The host made of `files` (name, text) with the default library and an entry point.
	TestHost MakeTestHost(const std::vector<std::pair<std::string, std::string>>& files);

	/// `script` (one file, `script.cf`) compiled against `host`, optimized or not.
	Scripting::CompiledScript CompileTestScript(const TestHost& host, const std::string& script, bool optimize = false,
		const std::vector<std::string>& allow = {});
} // namespace Testing
