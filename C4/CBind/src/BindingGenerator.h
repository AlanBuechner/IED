#pragma once
#include <string>
#include <vector>

namespace clang
{
	class ASTContext;
	class Decl;
	class RecordDecl;
	class CXXRecordDecl;
	class EnumDecl;
}

namespace CBind
{
	struct BindingOptions
	{
		std::string language = "c"; // "c" or "c++"
		std::vector<std::string> defines;
		std::vector<std::string> includeDirs;
	};

	class C4TypeMapper;

	/// Walks a parsed Clang AST and emits C4 declaration text (structs, enums, typedefs,
	/// function prototypes) for every top-level declaration in the target translation unit.
	class BindingGenerator
	{
	public:
		explicit BindingGenerator(const BindingOptions& opts) : m_Opts(opts) {}

		/// Generate C4 binding text for the given AST: every declaration of the translation unit, the
		/// headers it includes too, the first of each bare name winning. Returns empty on failure.
		std::string Generate(clang::ASTContext& ctx);

		/// C++ interop shim: emits a companion C++ file with one extern "C" forwarding wrapper per
		/// bound ctor, dtor, static method, instance method, and free function. Instance-method
		/// wrappers call through a typed pointer, so the C++ compiler performs real vtable
		/// dispatch for virtual methods - `base->Virtual()` from C4 resolves polymorphically.
		/// The generated bindings' [link] annotations reference these wrapper symbols. Returns ""
		/// when no wrappers are needed (C mode).
		std::string GenerateShim(clang::ASTContext& ctx);

	private:
		bool EmitRecord(const clang::RecordDecl* rec, const std::string& c4Name, C4TypeMapper& mapper, std::string& out);
		bool EmitEnum(const clang::EnumDecl* en, const std::string& c4Name, std::string& out);
		// cxxPrefix is the enclosing C++ namespace path ("a::b::", "" at global scope). It is used
		// to fully-qualify class references in the generated shim and to make wrapper symbols unique
		// per namespace; the emitted C4 declaration keeps a bare name (C4 has no namespaces).
		bool EmitFunction(const clang::Decl* decl, clang::ASTContext& ctx, C4TypeMapper& mapper, std::string& out, const std::string& cxxPrefix = "");
		bool EmitClass(const clang::CXXRecordDecl* rec, const std::string& c4Name, C4TypeMapper& mapper, clang::ASTContext& ctx, std::string& out, const std::string& cxxPrefix = "");

		const BindingOptions& m_Opts;
	};
}
