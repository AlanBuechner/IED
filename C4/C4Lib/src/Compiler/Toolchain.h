#pragma once
#include <filesystem>

namespace Scripting::Toolchain
{
	namespace fs = std::filesystem;

	/// Where C4 is installed (D-56): $C4_HOME when it is set, else the folder of the running executable. An
	/// install holds Bedrock/, C4Runtime/ (the runtime's sources), toolchain/ (clang, lld-link and clang's
	/// headers) and CBind/.
	fs::path Home();

	/// The running compiler's own executable.
	fs::path Executable();

	/// clang.exe in <home>/toolchain/bin; a development build also finds the repository's vendored LLVM above
	/// the executable. Empty when there is none.
	fs::path Clang();

	/// lld-link.exe beside Clang. Empty when there is none.
	fs::path LldLink();

	/// clang's own runtime libraries (`clang_rt.builtins` and the like), which clang's driver hands every link:
	/// lib/clang/<version>/lib/windows above Clang's folder. Empty when there are none.
	fs::path ClangRuntimeLibraries();

	/// The MSVC runtime and Windows SDK that setup.bat installs in the IED checkout (vendor/msvc, Visual Studio's
	/// layout without Visual Studio), found above the executable as a development build's vendored LLVM is: a toolchain
	/// in the checkout's store (toolchains/c4/<version>) reaches it too. clang and lld-link are given it (their
	/// -Xmicrosoft-windows-sys-root and /winsysroot); empty when there is none, and they find an installed Visual Studio.
	fs::path WindowsSysroot();

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
