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
	bool IsMainFile(const clang::Decl* d, const clang::ASTContext& ctx)
	{
		if (!d) return false;
		return ctx.getSourceManager().isWrittenInMainFile(d->getLocation());
	}

	const clang::TagDecl* UnderlyingTag(const clang::TypedefNameDecl* td)
	{
		const clang::QualType ut = td->getUnderlyingType();
		if (const auto* rt = dyn_cast<clang::RecordType>(ut.getTypePtr())) return rt->getDecl();
		if (const auto* et = dyn_cast<clang::EnumType>(ut.getTypePtr())) return et->getDecl();
		return nullptr;
	}

	bool IsCxx(const BindingOptions& opts) { return opts.language == "c++"; }

	FunctionBinding HowToBind(const clang::FunctionDecl* fn, const clang::ASTContext& ctx, const BindingOptions& opts)
	{
		const clang::FunctionDecl* definition = fn->getDefinition();
		const bool definedHere = definition && IsMainFile(definition, ctx);
		const bool forwardable = definedHere && !fn->isVariadic();
		if (!fn->isExternallyVisible()) return forwardable ? FunctionBinding::Wrapper : FunctionBinding::None;
		if (IsCxx(opts) && !HasCLanguageLinkage(fn))
			return IsExternalFreeFunction(fn) || forwardable ? FunctionBinding::Wrapper : FunctionBinding::None;
		const bool inlineOnly = definition && definition->isInlined() &&
			(ctx.getLangOpts().CPlusPlus || !definition->isInlineDefinitionExternallyVisible());
		return inlineOnly && forwardable ? FunctionBinding::Wrapper : FunctionBinding::Direct;
	}

	bool HasLayout(const clang::RecordDecl* rec)
	{
		return rec->isCompleteDefinition() && !rec->isInvalidDecl() && !rec->isDependentType();
	}

	/// Normalize a GUID string to lowercase canonical 8-4-4-4-12 form. If the text is not
	/// exactly 32 hex digits (ignoring dashes) it is returned unchanged so C4's parser can
	/// report a clear "[uuid(...)] is not a valid GUID" error instead of silently mangling it.
	std::string CanonicalizeGuidString(const std::string& raw)
	{
		std::string hex;
		for (char c : raw)
			if (c != '-') hex += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (hex.size() != 32) return raw;
		return hex.substr(0,8) + "-" + hex.substr(8,4) + "-" + hex.substr(12,4) + "-" +
		       hex.substr(16,4) + "-" + hex.substr(20,12);
	}

	/// The canonical GUID string for a record declared with __declspec(uuid(...)) / MIDL_INTERFACE,
	/// or "" if it has none.
	///
	/// Detection uses only INLINE libclang APIs: the vendored shared libclang does not export the
	/// out-of-line UuidAttr::getGuid()/classof() methods (linking against them fails), so we
	/// identify the attribute by its inline getKind() == attr::Uuid and then recover the GUID
	/// text from the source. The MIDL_INTERFACE("...") / __declspec(uuid("...")) macro argument
	/// is a plain 8-4-4-4-12 hex literal that immediately precedes the interface name in the
	/// header, so we match it directly rather than scanning a loose window (which could grab an
	/// unrelated GUID from a nearby declaration).
	std::string GetRecordUuid(const clang::RecordDecl* rec)
	{
		if (!rec || !rec->isCompleteDefinition()) return "";

		bool hasUuidAttr = false;
		auto hasUuid = [](const clang::RecordDecl* d) {
			for (const auto* attr : d->attrs())
				if (attr->getKind() == clang::attr::Uuid) return true;
			return false;
		};
		hasUuidAttr = hasUuid(rec);
		const clang::TagDecl* canon = rec->getCanonicalDecl();
		if (!hasUuidAttr && canon != rec)
			hasUuidAttr = hasUuid(static_cast<const clang::RecordDecl*>(canon));
		if (!hasUuidAttr) return "";

		const clang::SourceManager& sm = rec->getASTContext().getSourceManager();
		clang::SourceLocation loc = rec->getLocation(); // the interface name token
		if (loc.isInvalid()) return "";
		// NOTE: do NOT require isWrittenInMainFile here. The interface is almost always declared
		// in an INCLUDED SDK header (e.g. d3d12.h), not the top-level file CBind was invoked on.
		// The GUID text lives in that header's source buffer, which getBufferData(FileID) below
		// fetches correctly for any parsed file. Requiring the main file would silently drop every
		// real-world MIDL_INTERFACE/__declspec(uuid) declaration (the common case).

		llvm::StringRef buf;
		bool invalid = false;
		buf = sm.getBufferData(sm.getFileID(loc), &invalid);
		if (invalid || buf.empty()) return "";

		unsigned off = sm.getFileOffset(loc); // byte offset of the name in the buffer
		if (off >= buf.size()) return "";

		// Look back a bounded window from the interface name for the macro that carries the GUID.
		const unsigned win = 400;
		unsigned start = (off > win) ? (off - win) : 0u;
		std::string window(buf.substr(start, off - start).str());

		// Match the GUID string as it appears in the macro argument, e.g.
		//   MIDL_INTERFACE("905db94b-a00c-4140-9df5-2b64ca9ea357")  /  uuid("...")
		// (custom raw-string delimiter: the pattern itself ends with ")")
		static const std::regex guidRe(
			R"guid((?:MIDL_INTERFACE|uuid)\s*\(\s*"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})")guid");
		std::smatch m;
		if (!std::regex_search(window, m, guidRe)) return "";

		return CanonicalizeGuidString(m[1].str());
	}

		/// A free function is bindable (emitted as a C4 declaration + an extern "C" shim wrapper)
		/// only if its bare name can become a valid C++ identifier in the wrapper symbol
		/// `cbind_fn_<name>` AND it is not a compiler/SEH intrinsic that has no linkable symbol.
		/// Windows headers transitively pull in operator overloads (`operator==`) and MSVC
		/// intrinsics (`__va_start`, `_exception_code`, ...) that are not real linkable functions;
		/// wrapping them produces invalid C++ (the wrapper symbol would contain `=`) or calls to
		/// syntax-only compiler magic. Skipping them here keeps the shim compilable. The SAME
		/// predicate is applied in both the declaration pass and GenerateShim so no `[link]` ever
		/// references a wrapper that was not emitted.
		bool IsBindableFreeFunctionName(const std::string& name)
		{
			if (name.empty()) return false;
			for (char c : name)
				if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
					return false; // operator overloads, etc. - not a C4/C++ identifier
		static const std::unordered_set<std::string> intrinsics = {
			"__va_start", "_exception_code", "_exception_info",
			// Compiler-magic SEH builtins: clang synthesizes a body using `__except`, which this
			// build cannot lower (codegen AV). Never a legitimate interop target.
			"_abnormal_termination"
		};
		if (intrinsics.count(name)) return false;
		// Standard C library functions that the C4 compiler ALREADY provides as builtins
		// (C4Lib/src/Model/BuiltinDeclarations.cpp). Emitting a [link] binding for one of these
		// creates a second, differently-typed `strlen`/`memcpy`/... in the same overload set and
		// corrupts resolution of the builtin that Bedrock's default library calls unqualified.
		// The call resolves to the builtin, which emits the real CRT symbol (strlen, memcpy, ...),
		// so a binding is not just redundant but actively harmful. Keep this in sync with the
		// C memory/string/math sections of BuiltinDeclarations.cpp.
		static const std::unordered_set<std::string> c4BuiltinProvided = {
			"memcpy", "memset", "strlen", "strcmp", "strncmp", "memcmp", "printf", "fflush",
			"sqrtf", "sinf", "cosf", "tanf", "expf", "logf", "powf", "floorf", "ceilf", "fabsf",
			"sqrt", "sin", "cos", "tan", "exp", "log", "pow", "floor", "ceil", "fabs"
		};
		if (c4BuiltinProvided.count(name)) return false;
		// SIMD intrinsics (_mm*, _m*, __m*) expand to inline assembly with constant-immediate
		// operands - wrapping them as ordinary calls is ill-formed and they are never real
		// linkable functions.
		static const std::vector<std::string> intrinsicPrefixes = { "_mm", "_m_", "__m" };
		for (const auto& p : intrinsicPrefixes)
			if (name.compare(0, p.size(), p) == 0)
				return false;
		// Reserved / compiler-magic / CRT-internal names. Double-underscore identifiers are
		// reserved for the implementation and include intrinsics, SEH builtins, and CRT
		// internals (e.g. __threadid, __security_check_cookie) that have no stable public
		// declaration visible to the shim. A leading single underscore followed by a lowercase
		// letter is likewise reserved in C++ namespace scope (e.g. _onexit, _stricmp). These
		// are never legitimate game interop targets; skipping them keeps the shim compilable.
		if (name.size() >= 2 && name[0] == '_' && name[1] == '_')
			return false;
		if (name.size() >= 3 && name[0] == '_' && name[1] >= 'a' && name[1] <= 'z')
			return false;
		return true;
	}

	/// A free function is only a meaningful interop target when it is an *external* entry
	/// point - declared without a body in the TU (a real DLL export resolved from an import
	/// library, e.g. D3D12CreateDevice / CreateWindowExW). Functions that are DEFINED inline in
	/// a header (winnt.h SLIST/interlocked helpers, 128-bit atomic intrinsics, ...) must NOT be
	/// bound: the C++ shim's forwarding wrapper would compile their bodies into the object file,
	/// dragging in compiler-intrinsic and ntdll symbols (__atomic_compare_exchange_16, Rtl*) that
	/// no import library exports and breaking the link. We check the canonical declaration so a
	/// separate prototype whose definition lives elsewhere is also skipped.
		bool IsExternalFreeFunction(const clang::FunctionDecl* fn)
		{
			if (!fn) return false;
			const auto* canon = fn->getCanonicalDecl();
			return !canon->isDefined(); // no body anywhere -> external/DLL entry point
		}

	/// True when the function has C language linkage (declared inside an `extern "C"` block or
	/// in a C header). Such functions have UNMANGLED symbols - e.g. every Windows/DX API
	/// (CreateWindowExW, D3D12CreateDevice, Rtl* helpers) even when parsed as C++. They can be
	/// bound directly to their real symbol (like C-mode interop) and must NOT get an extern "C"
	/// forwarding wrapper: wrapping the whole windows.h+d3d12.h surface produced ~5,600 wrapper
	/// bodies whose dllimport/intrinsic references (Rtl*, __atomic_*) can't all be satisfied at
	/// final link. Only genuinely C++-mangled free functions need wrappers. getLanguageLinkage()
	/// is a pure AST query (walks the decl-context chain) - it does not require a MangleContext,
	/// which is what previously broke CBind's own static-lib link.
		bool HasCLanguageLinkage(const clang::FunctionDecl* fn)
		{
			if (!fn) return false;
			return fn->getLanguageLinkage() == clang::CLanguageLinkage;
		}

	/// True if the record declares at least one non-static virtual function among its OWN
	/// methods (inherited ones are excluded by `methods()`). This is exactly the condition for
	/// a class to INTRODUCE a vptr slot: C++ places the vptr in the first class of the
	/// hierarchy that has any virtual function, and derived classes inherit it.
	bool BindsDestructor(const clang::CXXRecordDecl* rec, const clang::CXXDestructorDecl* dtor)
	{
		if (!dtor || dtor->isDeleted()) return false;
		return !rec || !rec->isCompleteDefinition() || !rec->isAbstract() || dtor->isVirtual();
	}

	bool HasOwnVirtuals(const clang::CXXRecordDecl* rec)
	{
		if (!rec || !rec->isCompleteDefinition()) return false;
		for (const auto* m : rec->methods())
		{
			const auto* md = dyn_cast<clang::CXXMethodDecl>(m);
			if (md && !md->isStatic() && (md->isVirtual() || md->isPureVirtual()))
				return true;
		}
		return false;
	}

	/// True if any base class in the hierarchy introduces a vptr. Used to decide whether a
	/// derived binding needs its own `__vtable` placeholder field (it does not - it inherits).
	bool BaseHasVirtuals(const clang::CXXRecordDecl* rec)
	{
		for (const auto& base : rec->bases())
		{
			const auto* baseRec = base.getType()->getAsCXXRecordDecl();
			if (!baseRec) continue;
			if (HasOwnVirtuals(baseRec) || BaseHasVirtuals(baseRec))
				return true;
		}
		return false;
	}
} // namespace CBind
