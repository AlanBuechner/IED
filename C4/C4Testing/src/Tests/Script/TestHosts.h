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

	/// One project of a ProjectHost: its name, what it depends on, and its files (name, text) in a folder of its own.
	struct TestProject
	{
		std::string name;
		std::vector<std::string> depends;
		std::vector<std::pair<std::string, std::string>> files;
	};

	/// The host of `projects`, as `C4.exe --projects` builds one, the last the root (with the entry point), with the
	/// default library.
	TestHost MakeProjectTestHost(const std::vector<TestProject>& projects);

	/// `script` (one file, `script.cf`) compiled against `host`, optimized or not.
	Scripting::CompiledScript CompileTestScript(const TestHost& host, const std::string& script, bool optimize = false,
		const std::vector<std::string>& allow = {}, bool onlyAllowed = false);
} // namespace Testing
