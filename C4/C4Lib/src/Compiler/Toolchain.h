#pragma once
#include <filesystem>

namespace Scripting::Toolchain
{
	namespace fs = std::filesystem;

	/// Where C4 is installed (D-56): $C4_HOME when it is set, else the folder of the running executable. An
	/// install holds Bedrock/, C4Runtime/ (the runtime's sources), toolchain/ (clang, lld-link and clang's
	/// headers) and CBind/.
	fs::path Home();

	/// clang.exe in <home>/toolchain/bin; a development build also finds the repository's vendored LLVM above
	/// the executable. Empty when there is none.
	fs::path Clang();

	/// The runtime's C and C++ sources: <home>/C4Runtime, or a development build's repository copy. Empty when
	/// there are none.
	fs::path Runtime();

	/// CBind.exe: <home>/CBind, or a development build's tool folder beside the executable's. Empty when absent.
	fs::path CBind();

	/// The folder holding C4Script.dll and its import library C4Script.lib (the script host, docs/runtime/04):
	/// <home>/C4Script, or a development build's tool folder beside the executable's. Empty when absent.
	fs::path ScriptHost();

	/// The default library: <home>/Bedrock, or a development build's repository copy (the language server's
	/// build folder has none of its own).
	fs::path Bedrock();
}
