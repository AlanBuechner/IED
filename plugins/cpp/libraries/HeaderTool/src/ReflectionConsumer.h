#ifndef LLVM_CLANG_FRONTEND_REFLECTIONXCONSUMER_H
#define LLVM_CLANG_FRONTEND_REFLECTIONXCONSUMER_H

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/LLVM.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Serialization/ASTDeserializationListener.h"
#include "clang/Tooling/Tooling.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include "StaticReflection/ReflectionData.h"

/* Declares llvm::cl::extrahelp. */
#include "llvm/Support/CommandLine.h"

#include <filesystem>
#include <memory>
#include <vector>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <iostream>

using namespace clang;
using namespace clang::ast_matchers;
namespace fs = std::filesystem;


namespace clang {

	std::string GetAnnotations(Attr const* attr);
	std::string GetAnnotations(const AttrVec& attrs);

	// --------------------------- Reflected Class --------------------------- //

	Reflection::Class BuildClass(const CXXRecordDecl* rec, ASTContext* ctx, const std::string& attrs);


	// --------------------------- Class finder --------------------------- //
	class ClassFinder : public MatchFinder::MatchCallback {
	public:

		virtual void run(MatchFinder::MatchResult const& result) override;

		virtual void onStartOfTranslationUnit() override {}
		virtual void onEndOfTranslationUnit() override {}

	protected:
		ASTContext* m_context;
		SourceManager* m_sourceman;
	};

	// --------------------------- Reflection Consumer --------------------------- //
	class ReflectionConsumer : public ASTConsumer {
	public:
		ReflectionConsumer() {}
		virtual ~ReflectionConsumer() override {}

		static void Init(const fs::path& generatedFolder, const fs::path& sourceFolder);

		virtual void HandleTranslationUnit(ASTContext& Ctx) override;

		static Reflection::File& GetFile(fs::path path);

		static fs::path s_GeneratedFolder;
		static fs::path s_SourceFolder;
		static std::unordered_map<fs::path, Reflection::File> s_ReflectedFiles;
	};

	class ReflectionAction : public clang::ASTFrontendAction {
	public:
		virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
			clang::CompilerInstance& Compiler, llvm::StringRef InFile) {
			return std::make_unique<ReflectionConsumer>();
		}
	};

} // namespace clang

#endif