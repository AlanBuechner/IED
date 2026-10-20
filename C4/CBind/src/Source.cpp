#include "BindingGenerator.h"
#include "C4TypeMapper.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"

#include <filesystem>
#include <fstream>
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
			else if (a.size() > 2 && a.compare(0, 2, "-m") == 0) out.targetFlags.push_back(a);
		}
		return !out.header.empty() && !out.outFile.empty();
	}

		class BindingConsumer : public ASTConsumer
	{
	public:
		void HandleTranslationUnit(ASTContext& ctx) override
		{
			BindingOptions opts;
			opts.language = g_args.language;
			opts.defines = g_args.defines;
			opts.includeDirs = g_args.includeDirs;

			BindingGenerator gen(opts);
			std::string text = gen.Generate(ctx);

			fs::path outPath(g_args.outFile);
			if (outPath.has_parent_path())
				fs::create_directories(outPath.parent_path());
			std::ofstream f(outPath, std::ios::trunc);
			f << text;

			// C++ virtual dispatch: emit a companion C++ shim with one vtable trampoline per
			// virtual method. InteropPass compiles it as a foreign object and links it in.
			std::string shim = gen.GenerateShim(ctx);
			if (!shim.empty())
			{
				fs::path shimPath(outPath.filename().stem().string() + ".cbind_shim.cpp");
				shimPath = outPath.parent_path() / shimPath;
			std::ofstream sf(shimPath, std::ios::trunc);
			// <new> is required for placement new in ctor wrappers; the user's header may not pull it in.
			sf << "#define _CRT_SECURE_NO_WARNINGS\n";
			sf << "#include <new>\n";
			sf << "#include \"" + fs::absolute(g_args.header).filename().string() + "\"\n";
				sf << shim;
			}
		}
	};

	class BindingAction : public ASTFrontendAction
	{
	public:
		virtual std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, llvm::StringRef)
		{
			return std::make_unique<BindingConsumer>();
		}
	};
}

int main(int argc, char** argv)
{
	CBindArgs args;
	if (!ParseArgs(argc, argv, args))
	{
		std::cerr << "usage: CBind.exe <header> <outFile> [--lang c|c++] [-D name=value]... [-I dir]... [-march=cpu] [-m[no-]feature]...\n";
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

	g_args = args; // action reads these at run time
	g_args.header = headerPath.string();

	auto db = std::make_unique<FixedCompilationDatabase>(dir, cmdLine);
	ClangTool tool(*db, {headerPath.string()});

	int result = tool.run(newFrontendActionFactory<BindingAction>().get());
	return result;
}
