#pragma once
// What a declaration in a bound header is: in the main file, external, with C linkage, with virtuals, its GUID.
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
#include <utility>

namespace CBind
{
	bool IsMainFile(const clang::Decl* d, const clang::ASTContext& ctx);
	const clang::TagDecl* UnderlyingTag(const clang::TypedefNameDecl* td);
	bool IsCxx(const BindingOptions& opts);
	std::string CanonicalizeGuidString(const std::string& raw);
	std::string GetRecordUuid(const clang::RecordDecl* rec);
	bool IsBindableFreeFunctionName(const std::string& name);
	bool IsExternalFreeFunction(const clang::FunctionDecl* fn);
	bool HasCLanguageLinkage(const clang::FunctionDecl* fn);
	bool HasOwnVirtuals(const clang::CXXRecordDecl* rec);
	bool BaseHasVirtuals(const clang::CXXRecordDecl* rec);

	/// Recursively visit every declaration in the translation unit, descending into named and
	/// anonymous namespaces. Real C/C++ headers put most of their API inside namespaces (std::,
	/// DirectX::, etc.), so binding only top-level decls would silently miss them. Each visited
	/// decl is still filtered by IsMainFile at the call site so transitive #includes are skipped.
	/// `prefix` is the enclosing namespace path ("a::b::"); anonymous namespaces contribute no
	/// prefix of their own. The visitor may use it to fully-qualify C++ references in the shim.
	template <typename F>
	void WalkDecls(const clang::DeclContext* dc, const std::string& prefix, F&& f)
	{
		for (const auto* d : dc->decls())
		{
			if (const auto* ns = dyn_cast<clang::NamespaceDecl>(d))
			{
				std::string childPrefix = ns->getNameAsString().empty()
					? prefix
					: prefix + ns->getNameAsString() + "::";
				WalkDecls(ns, childPrefix, std::forward<F>(f));
			}
			else if (const auto* ls = dyn_cast<clang::LinkageSpecDecl>(d))
			{
				// extern "C" / extern "C++" blocks: their members are top-level decls with the
				// same namespace prefix. Very common in interop headers - must descend or the
				// whole block is invisible to the binding generator.
				WalkDecls(ls, prefix, std::forward<F>(f));
			}
			else
				f(d, prefix);
		}
	}
} // namespace CBind
