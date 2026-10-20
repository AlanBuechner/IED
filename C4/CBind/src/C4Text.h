#pragma once
// C4 text for bound declarations: identifiers, parameter and return types, fields, static data, wrapper names.
#include "BindingGenerator.h"
#include "C4TypeMapper.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Mangle.h"
#include <string>

namespace CBind
{
	std::string StripAttributes(const std::string& s);
	std::string SafeId(const std::string& s);
	std::string WrapperName(const std::string& className, const std::string& c4MethodName);
	bool MapParams(const clang::FunctionDecl* fn, C4TypeMapper& mapper, std::string& out);
	bool MapReturn(const clang::FunctionDecl* fn, C4TypeMapper& mapper, std::string& out);
	std::string AccessOf(clang::AccessSpecifier access);
	std::string OverloadSymbol(const std::string& name, size_t index, size_t overloads);
	void EmitStaticData(const clang::CXXRecordDecl* rec, C4TypeMapper& mapper, clang::ASTContext& ctx, std::string& out);
} // namespace CBind
