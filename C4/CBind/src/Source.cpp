#include "BindingGenerator.h"
#include "C4TypeMapper.h"
#include "MacroConstants.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace clang;
using namespace clang::tooling;
using namespace CBind;

namespace
{
	struct CBindArgs
	{
		std::string header;
		std::string outFile;
		std::string language = "c";
		std::vector<std::string> defines;
		std::vector<std::string> includeDirs;
		std::vector<std::string> targetFlags; // -march=<cpu>, -m<feature>, -mno-<feature>: the C4 build's processor
		std::string windowsSysroot;            // --winsysroot: the MSVC and SDK kit the compiler uses (Toolchain::WindowsSysroot)
		std::vector<std::string> clangArgs;   // what clang parses the header with (main builds it)
	};

	CBindArgs g_args; // set in main() before the tool runs; read by the default-constructed action

	bool ParseArgs(int argc, char** argv, CBindArgs& out)
	{
		if (argc < 3) return false;
		out.header = argv[1];
		out.outFile = argv[2];
		for (int i = 3; i < argc; ++i)
		{
			std::string a = argv[i];
			if (a == "--lang" && i + 1 < argc) out.language = argv[++i];
			else if (a == "-D" && i + 1 < argc) out.defines.push_back(argv[++i]);
			else if (a == "-I" && i + 1 < argc) out.includeDirs.push_back(argv[++i]);
			else if (a == "--winsysroot" && i + 1 < argc) out.windowsSysroot = argv[++i];
			else if (a.size() > 2 && a.compare(0, 2, "-m") == 0) out.targetFlags.push_back(a);
		}
		return !out.header.empty() && !out.outFile.empty();
	}

		class BindingConsumer : public ASTConsumer
	{
	public:
		explicit BindingConsumer(const Preprocessor& pp) : m_Preprocessor(pp) {}

		void HandleTranslationUnit(ASTContext& ctx) override
		{
			BindingOptions opts;
			opts.language = g_args.language;
			opts.defines = g_args.defines;
			opts.includeDirs = g_args.includeDirs;

			BindingGenerator gen(opts);
			std::string text = gen.Generate(ctx);
			text += ConstantDeclarations(ConstantCandidates(m_Preprocessor), gen.BoundNames(), g_args.header, g_args.clangArgs, g_args.language == "c++");

			// LLVM's file calls take paths longer than MAX_PATH (a deep build folder); the standard streams do not.
			fs::path outPath(g_args.outFile);
			if (outPath.has_parent_path())
				llvm::sys::fs::create_directories(outPath.parent_path().string());
			std::error_code ec;
			llvm::raw_fd_ostream f(outPath.string(), ec, llvm::sys::fs::OF_TextWithCRLF);
			f << text;

			// C++ virtual dispatch: emit a companion C++ shim with one vtable trampoline per
			// virtual method. InteropPass compiles it as a foreign object and links it in.
			// A C header's shim is C (BUG-283): it forwards the header's static and inline functions. A shim left from an
			// earlier run of either language goes first, so the compiler never links a stale one.
			std::string shim = gen.GenerateShim(ctx);
			const bool cxx = g_args.language == "c++";
			const std::string stem = (outPath.parent_path() / outPath.filename().stem()).string();
			llvm::sys::fs::remove(stem + ".cbind_shim.cpp");
			llvm::sys::fs::remove(stem + ".cbind_shim.c");
			if (!shim.empty())
			{
				llvm::raw_fd_ostream sf(stem + (cxx ? ".cbind_shim.cpp" : ".cbind_shim.c"), ec, llvm::sys::fs::OF_TextWithCRLF);
				sf << "#define _CRT_SECURE_NO_WARNINGS\n";
				if (cxx) sf << "#include <new>\n"; // placement new in constructor wrappers; the header may not include it
				sf << "#include \"" + fs::absolute(g_args.header).filename().string() + "\"\n";
				// The shim forwards what the header declares, deprecated or not (BUG-326): its deprecations are for the
				// header's callers.
				sf << "#pragma clang diagnostic ignored \"-Wdeprecated-declarations\"\n";
				sf << shim;
			}
		}

	private:
		const Preprocessor& m_Preprocessor;   // the header's macros, read once the parse is done
	};

	class BindingAction : public ASTFrontendAction
	{
	public:
		virtual std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& compiler, llvm::StringRef)
		{
			return std::make_unique<BindingConsumer>(compiler.getPreprocessor());
		}
	};
}

int main(int argc, char** argv)
{
	CBindArgs args;
	if (!ParseArgs(argc, argv, args))
	{
		std::cerr << "usage: CBind.exe <header> <outFile> [--lang c|c++] [-D name=value]... [-I dir]... [-march=cpu] [-m[no-]feature]... [--winsysroot dir]\n";
		return 1;
	}

	fs::path headerPath(args.header);
	if (!fs::exists(headerPath))
	{
		std::cerr << "cbind: header not found: " << args.header << "\n";
		return 1;
	}
	headerPath = fs::absolute(headerPath);
	std::string dir = headerPath.parent_path().string();

	std::vector<std::string> cmdLine;
	// MUST match CompileForeignSource's target exactly: the [link(...)] symbols CBind emits are
	// taken from this tool's MangleContext and must be byte-identical to the symbols in the
	// foreign object file. A target mismatch would switch the mangling scheme (MSVC vs Itanium)
	// and silently break link resolution.
	cmdLine.push_back("-target");
	cmdLine.push_back("x86_64-pc-windows-msvc");
	cmdLine.push_back("-mstackrealign");
	cmdLine.push_back("-x");
	cmdLine.push_back(args.language);
	for (const auto& d : args.defines) cmdLine.push_back("-D" + d);
	for (const auto& i : args.includeDirs) cmdLine.push_back("-I" + i);
	cmdLine.insert(cmdLine.end(), args.targetFlags.begin(), args.targetFlags.end());
	if (!args.windowsSysroot.empty())
	{
		// The headers the foreign objects compile with (CompileForeignSource), not an installed Visual Studio's.
		cmdLine.push_back("-Xmicrosoft-windows-sys-root");
		cmdLine.push_back(args.windowsSysroot);
	}

	g_args = args; // action reads these at run time
	g_args.header = headerPath.string();
	g_args.clangArgs = cmdLine;

	auto db = std::make_unique<FixedCompilationDatabase>(dir, cmdLine);
	ClangTool tool(*db, {headerPath.string()});

	int result = tool.run(newFrontendActionFactory<BindingAction>().get());
	return result;
}
