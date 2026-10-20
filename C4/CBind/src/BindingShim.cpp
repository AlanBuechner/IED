#include "BindingGenerator.h"
#include "C4Text.h"
#include "DeclFacts.h"
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
	std::string BindingGenerator::GenerateShim(clang::ASTContext& ctx)
	{
		const auto* tu = ctx.getTranslationUnitDecl();
		std::string out;
		bool emittedAny = false;

		// Namespaces that contain at least one bound decl. The shim is compiled at global scope,
		// but getAsString() spells namespaced types unqualified (e.g. "const V &"), so we emit a
		// `using namespace` directive for each such namespace to make those spellings resolve.
		std::vector<std::string> usingNs;
		std::unordered_set<std::string> usingNsSeen;
		auto recordNs = [&](const std::string& prefix) {
			if (prefix.empty()) return;
			std::string ns = prefix;           // "a::b::"
			while (!ns.empty() && ns.back() == ':') ns.pop_back();
			if (!usingNsSeen.count(ns)) { usingNsSeen.insert(ns); usingNs.push_back(ns); }
		};

		auto typeStr = [&](clang::QualType t) { return StripAttributes(t.getAsString(ctx.getLangOpts())); };
		// A parameter declaration with its name in the declarator: `int32_t (*callback)(int32_t)`, not
		// `int32_t (*)(int32_t) callback`.
		auto declarator = [&](clang::QualType t, const std::string& name) {
			std::string text;
			llvm::raw_string_ostream os(text);
			t.print(os, clang::PrintingPolicy(ctx.getLangOpts()), name);
			return StripAttributes(os.str());
		};

		// Build a C++ parameter list "T1 n1, T2 n2" plus the argument-name list for forwarding.
		auto paramList = [&](const clang::FunctionDecl* fn, std::string& namesOut) {
			std::string params;
			namesOut.clear();
			bool first = true;
			unsigned idx = 0;
			for (auto it = fn->param_begin(); it != fn->param_end(); ++it)
			{
				if (!first) params += ", ";
				first = false;
				std::string pname = (*it)->getNameAsString();
				if (pname.empty())
					pname = "a" + std::to_string(idx); // unnamed param - synthesize a name
				params += declarator((*it)->getType(), pname);
				namesOut += (namesOut.empty() ? "" : ", ") + pname;
				++idx;
			}
			return params;
		};

		// Bind the full translation unit (consistent with Generate). Free functions and classes
		// are deduped first-wins by bare C4 name so transitive-header overloads don't clash.
		std::unordered_set<std::string> fnSeen;
		std::unordered_set<std::string> classSeen;
		WalkDecls(tu, "", [&](const clang::Decl* d, const std::string& prefix) {
			// Free functions (non-method) in C++ mode are mangled by the C++ compiler; emit an
			// extern "C" wrapper that forwards to the real function so the [link] reference
			// links. Inline-defined functions (e.g. GetMessageW) get a wrapper too so their
			// [link] symbol always exists. The wrapper symbol stays bare (C4 has no namespaces);
			// the call target is fully qualified.
			if (const auto* fn = dyn_cast<clang::FunctionDecl>(d))
			{
			if (isa<clang::CXXMethodDecl>(fn))
				return;
			std::string name = fn->getNameAsString();
			if (!IsBindableFreeFunctionName(name) || fnSeen.count(name))
				return;
			// Exactly the functions EmitFunction binds to a wrapper (BUG-283): a C++-linkage function, or one with no
			// symbol of its own that the bound header defines. A C-linkage export is reached at its own symbol.
			if (HowToBind(fn, ctx, m_Opts) != FunctionBinding::Wrapper)
				return;
			fnSeen.insert(name);
				recordNs(prefix);
				std::string ret = typeStr(fn->getReturnType());
				std::string params, names;
				params = paramList(fn, names);
				const bool cxx = IsCxx(m_Opts);
				const bool returns = !fn->getReturnType()->isVoidType();
				out += "// free-function wrapper for " + prefix + name + "\n" + (cxx ? "extern \"C\" " : "")
					+ ret + " cbind_fn_" + name + "(";
				out += params.empty() && !cxx ? "void" : params;
				out += ") {\n\t" + std::string(returns ? "return " : "") + prefix + name + "(" + names + ");\n}\n\n";
				emittedAny = true;
				return;
			}
			if (!IsCxx(m_Opts))
				return; // a C header's records have no methods to forward

			const auto* rec = dyn_cast<clang::CXXRecordDecl>(d);
			if (!rec || !rec->isCompleteDefinition() || rec->getNameAsString().empty())
				return;
			std::string className = rec->getNameAsString();
			if (classSeen.count(className))
				return;
			classSeen.insert(className);
			recordNs(prefix);
			const std::string cxxName = prefix + className;   // fully-qualified C++ reference
			const std::string& symBase = className;            // bare wrapper-symbol base (matches bindings)

		// Collect ctors/dtors/methods exactly as EmitClass does, so wrapper names line up.
		// Special members (operator= etc.) are implicit C++ plumbing - never bound.
		std::vector<const clang::CXXConstructorDecl*> ctors;
		std::vector<const clang::CXXDestructorDecl*> dtors;
		std::unordered_map<std::string, std::vector<const clang::CXXMethodDecl*>> methods;
		for (const auto* m : rec->methods())
		{
			if (const auto* ctor = dyn_cast<clang::CXXConstructorDecl>(m))
			{
				if (!ctor->isDeleted() && !ctor->isDefaulted()) ctors.push_back(ctor);
			}
			else if (const auto* dtor = dyn_cast<clang::CXXDestructorDecl>(m))
			{
				if (!dtor->isDeleted()) dtors.push_back(dtor);
			}
			else
			{
				const std::string mname = m->getNameAsString();
			if (mname.rfind("operator", 0) == 0)
				continue; // operator=, operator==, conversions - not C4-bindable
			methods[mname].push_back(m);
		}
	}

	auto emitCtor = [&](const clang::CXXConstructorDecl* ctor, const std::string& c4Name) {
			std::string params, names;
			params = paramList(ctor, names);
				out += "// ctor wrapper for " + cxxName + "\nextern \"C\" void "
					+ WrapperName(symBase, c4Name) + "(void* self";
			if (!params.empty()) out += ", " + params;
			out += ") {\n\tnew (self) " + cxxName + "(" + names + ");\n}\n\n";
		};

		// Implicit default ctor (no user-defined ctor): bind it so `new T()` runs real C++ init.
		if (ctors.empty())
		{
			for (const auto* m : rec->methods())
			{
				const auto* ctor = dyn_cast<clang::CXXConstructorDecl>(m);
				if (ctor && !ctor->isDeleted() && ctor->getNumParams() == 0)
				{
					out += "// implicit default ctor wrapper for " + cxxName + "\nextern \"C\" void "
						+ WrapperName(symBase, "Construct") + "(void* self) {\n\tnew (self) "
						+ cxxName + "();\n}\n\n";
					break;
				}
			}
		}
		for (size_t i = 0; i < ctors.size(); ++i)
			emitCtor(ctors[i], OverloadSymbol("Construct", i, ctors.size()));

		// Destructor ([operator("delete")]): scope exit, delete and the GC each run it exactly once.
		for (const auto* dtor : dtors)
		{
			out += "// dtor wrapper for " + cxxName + "\nextern \"C\" void "
				+ WrapperName(symBase, "Finalize") + "(void* self) {\n\tstatic_cast<" + cxxName
				+ "*>(self)->~" + className + "();\n}\n\n";
		}

			// Static methods: no `this`. Free-function wrapper forwarding to the real static.
			for (auto& [name, group] : methods)
			{
				for (size_t index = 0; index < group.size(); ++index)
				{
					const clang::CXXMethodDecl* m = group[index];
					const std::string c4Name = OverloadSymbol(name, index, group.size());
					if (m->isStatic())
					{
					std::string params, names;
					params = paramList(m, names);
					out += "// static wrapper for " + cxxName + "::" + name + "\nextern \"C\" "
						+ typeStr(m->getReturnType()) + " " + WrapperName(symBase, "st_" + c4Name) + "(";
					if (!params.empty()) out += params;
					out += ") {\n\treturn " + cxxName + "::" + name + "(" + names + ");\n}\n\n";
				}
				else
				{
					// Instance (and virtual) method: forward through a typed pointer. For virtual
					// methods the C++ compiler emits a vtable call, so dispatch is polymorphic -
					// exactly matching C++ semantics with no manual slot computation.
					std::string params, names;
					params = paramList(m, names);
						out += "// method wrapper for " + cxxName + "::" + name
							+ (m->isVirtual() || m->isPureVirtual() ? " [virtual]" : "") + "\nextern \"C\" "
							+ typeStr(m->getReturnType()) + " " + WrapperName(symBase, c4Name) + "(void* self";
						if (!params.empty()) out += ", " + params;
						out += ") {\n\treturn (*static_cast<" + cxxName + "*>(self))." + name + "(" + names + ");\n}\n\n";
				}
				}
			}

			emittedAny = true;
		});

		if (!emittedAny)
			return "";

		// Prepend `using namespace` directives so the unqualified type spellings produced by
		// getAsString() (e.g. "const V &") resolve for namespaced types. These must precede the
		// wrappers that use them; Source.cpp already prepends the #include lines.
		std::string result;
		for (const auto& ns : usingNs)
			if (IsCxx(m_Opts)) result += "using namespace " + ns + ";\n";
		return result + out;
	}
} // namespace CBind
