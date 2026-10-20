#define IMPLEMENT_LOGGER
#include "ReflectionConsumer.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/CommandLine.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

using namespace clang::tooling;
using namespace llvm;

int main(int argc, char** argv) {
	// Register custom CLI options in this TU only (NOT in header)
	static llvm::cl::opt<std::string> ClGeneratedFolder(
		"generated-folder", llvm::cl::desc("Path to the generated output folder"),
		llvm::cl::init("generated"));

	static llvm::cl::opt<std::string> ClSourceFolder(
		"source-folder", llvm::cl::desc("Path to the source folder"),
		llvm::cl::init("src"));

	// Parse arguments FIRST so our custom options are populated
	auto ExpectedParser = CommonOptionsParser::create(argc, (const char**)argv, cl::getGeneralCategory(), cl::ZeroOrMore);
	if (!ExpectedParser) {
		// Fail gracefully for unsupported options.
		llvm::errs() << ExpectedParser.takeError();
		return 1;
	}

	// Initialize reflection system AFTER parsing with the actual CLI values
	ReflectionConsumer::Init(ClGeneratedFolder.getValue(), ClSourceFolder.getValue());

	ClangTool Tool(ExpectedParser->getCompilations(), ExpectedParser->getSourcePathList());
	int result = Tool.run(newFrontendActionFactory<ReflectionAction>().get());

	return 0;
}