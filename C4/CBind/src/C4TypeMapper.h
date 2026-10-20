#pragma once
#include "clang/AST/Type.h"
#include "clang/AST/CanonicalType.h"
#include <string>
#include <unordered_map>

namespace clang
{
	class ASTContext;
	class TagDecl;
}

namespace CBind
{
	/// Maps Clang types to C4 source type spellings.
	///
	/// Records/enums are resolved by their (stable) TagDecl* against a pre-registered map, so
	/// both named structs and anonymous-struct-typedefs work. ABI safety: every pointer lowers
	/// to an opaque LLVM `ptr` regardless of pointee, and primitives map by bit-width, so the
	/// generated bindings stay ABI-compatible even when a complex pointee is approximated as `void*`.
	class C4TypeMapper
	{
	public:
		explicit C4TypeMapper(clang::ASTContext& ctx) : m_Ctx(ctx) {}

		void RegisterRecord(const clang::TagDecl* decl, const std::string& c4Name) { m_Records[decl] = c4Name; }
		void RegisterEnum(const clang::TagDecl* decl, const std::string& c4Name) { m_Enums[decl] = c4Name; }
		/// A typedef bound as a C4 alias (`alias Name = <type>;`). Resolved in MapSingle after
		/// records/enums so that an alias pointing at a named struct resolves to the struct's C4 name.
		void RegisterAlias(const std::string& c4Name, const std::string& targetSpelling) { m_Aliases[c4Name] = targetSpelling; }

		/// Emit a C4 type spelling for `t`. Returns false if the type cannot be represented.
		bool Map(clang::QualType t, std::string& out);

	private:
		static std::string WithConst(const std::string& type, bool isConst);
		bool MapSingle(clang::QualType t, bool allowVoid, std::string& out);
		bool MapFunction(const clang::FunctionProtoType* fpt, std::string& out);

		clang::ASTContext& m_Ctx;
		std::unordered_map<const clang::TagDecl*, std::string> m_Records;
		std::unordered_map<const clang::TagDecl*, std::string> m_Enums;
		std::unordered_map<std::string, std::string> m_Aliases; // typedef name -> C4 target spelling
	};
}
