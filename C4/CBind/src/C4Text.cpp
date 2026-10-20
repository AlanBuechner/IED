#include "C4Text.h"
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
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cstdio>
#include <regex>

namespace CBind
{
	/// Remove `__attribute__((...))` clauses from a C++ type spelling. getAsString() embeds
	/// calling-convention attributes (e.g. `void (*)() __attribute__((cdecl))`) which are
	/// malformed in parameter position when re-emitted into the shim. On x64 Windows there is
	/// a single calling convention, so dropping the attribute is ABI-safe.
	std::string StripAttributes(const std::string& s)
	{
		std::string out;
		size_t i = 0;
		while (i < s.size())
		{
			if (s.compare(i, 13, "__attribute") == 0)
			{
				size_t j = i + 13;
				while (j < s.size() && isspace((unsigned char)s[j])) ++j;
				if (j < s.size() && s[j] == '(')
				{
					int depth = 0;
					while (j < s.size())
					{
						if (s[j] == '(') ++depth;
						else if (s[j] == ')') { --depth; if (depth == 0) { ++j; break; } }
						++j;
					}
					i = j;
					continue;
				}
			}
			out += s[i];
			++i;
		}
		return out;
	}

	/// C4 identifier-safe: replace any non-alnum char with '_'. Used for wrapper symbols.
	std::string SafeId(const std::string& s)
	{
		std::string r;
		for (char c : s)
			r += (isalnum((unsigned char)c) || c == '_') ? c : '_';
		return r.empty() ? "_" : r;
	}

	/// Name of the extern "C" C++ wrapper for a bound method. The wrapper is emitted into the
	/// shim file (compiled as real C++) and forwards to the actual method, so the C++ compiler
	/// handles virtual dispatch, overloads, ctors and dtors - no in-process name mangling
	/// (which would pull libclang-only dllimport symbols) is required. The wrapper's `self`
	/// parameter is the object pointer, matching C4's hidden-this calling convention.
	std::string WrapperName(const std::string& className, const std::string& c4MethodName)
	{
		return "cbind_" + SafeId(className + "_" + c4MethodName);
	}

	/// Parameter list text for a method binding: `Type name, Type name`. Parameters without
	/// names (common in redeclarations) get synthesized names so both the C4 declaration and
	/// any shim forwarding call remain well-formed.
	bool MapParams(const clang::FunctionDecl* fn, C4TypeMapper& mapper, std::string& out)
	{
		std::string params;
		bool first = true;
		unsigned idx = 0;
		for (auto it = fn->param_begin(); it != fn->param_end(); ++it)
		{
			const auto* p = *it;
			std::string ty;
			if (!mapper.Map(p->getType(), ty))
				return false; // can't represent a param - caller skips the whole function
			if (!first) params += ", ";
			first = false;
			std::string pname = p->getNameAsString();
			if (pname.empty())
				pname = "a" + std::to_string(idx);
			params += ty + " " + pname;
			++idx;
		}
		out = params;
		return true;
	}

	bool MapReturn(const clang::FunctionDecl* fn, C4TypeMapper& mapper, std::string& out)
	{
		return mapper.Map(fn->getReturnType(), out);
	}

	// A C++ member's access as a C4 modifier: C4 code sees a bound class as C++ code would.
	std::string AccessOf(clang::AccessSpecifier access)
	{
		if (access == clang::AS_private) return "private ";
		if (access == clang::AS_protected) return "protected ";
		return "";
	}

	// A bound overload's own C4 name is the C++ name (C4 overloads by parameter types); its wrapper symbol
	// carries the overload's index in declaration order, which also separates overloads of the same arity.
	std::string OverloadSymbol(const std::string& name, size_t index, size_t overloads)
	{
		return overloads > 1 ? name + "_" + std::to_string(index) : name;
	}

	// A static data member is the C++ variable itself, linked by its decorated symbol and read and written in place.
	void EmitStaticData(const clang::CXXRecordDecl* rec, C4TypeMapper& mapper, clang::ASTContext& ctx, std::string& out)
	{
		clang::ASTNameGenerator mangler(ctx);
		for (const auto* decl : rec->decls())
		{
			const auto* var = dyn_cast<clang::VarDecl>(decl);
			if (!var || !var->isStaticDataMember()) continue;
			std::string ty;
			if (!mapper.Map(var->getType(), ty)) continue;
			const std::string symbol = mangler.getName(var);
			out += "\n\t[link(\"" + symbol + "\")] " + (AccessOf(var->getAccess()).empty() ? std::string("public ") : AccessOf(var->getAccess())) +
				"static " + ty + " " + var->getNameAsString() + ";";
		}
	}
} // namespace CBind
