#pragma once
// A bound record's C4 members, laid out byte for byte as clang lays out the C or C++ record (BUG-279 to BUG-282).
#include "C4TypeMapper.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include <cstdint>
#include <string>
#include <vector>

namespace CBind
{
	/// Writes a record's members so that C4, laying them out naturally, puts each where clang does. C4 has no
	/// bitfields, packing or alignment overrides, so:
	/// - padding (`uint8[n] __padN`) goes before any member C4 would not place at clang's offset by itself (always
	///   before a record-typed member, whose C4 alignment may be smaller);
	/// - a run of bitfields is the integers that store it (`uint32 __bits_a_b`), read with shifts;
	/// - a member C4 cannot spell (`__int128`) or place (a packed record's) is its bytes, under its own name; a
	///   flexible array member is left out, as it has no size;
	/// - an anonymous union is a C4 anonymous union of every arm, its struct arms struct views; one inside a view is
	///   its bytes, as a view holds no union;
	/// - a union too small for clang's size gets an arm of bytes, a struct padding to clang's size.
	class RecordText
	{
	public:
		RecordText(clang::ASTContext& ctx, C4TypeMapper& mapper, std::string& out) : m_Ctx(ctx), m_Mapper(mapper), m_Out(out) {}

		/// The members of a struct or class from byte `start` on (C4 places a C++ class's bases and vtable pointer
		/// itself), then padding to clang's size.
		void StructBody(const clang::RecordDecl* rec, uint64_t start);
		/// The arms of a named union.
		void UnionBody(const clang::RecordDecl* rec);

	private:
		// Where C4 has placed the members so far, and the least its alignment of them can be (from the members it
		// aligns as clang does).
		struct Cursor { uint64_t offset = 0; uint64_t align = 1; };

		void Fields(const clang::RecordDecl* rec, uint64_t base, Cursor& cursor, const std::string& indent, uint64_t limit, bool inView);
		void Bitfields(const std::vector<const clang::FieldDecl*>& run, uint64_t start, uint64_t end, Cursor& cursor,
			const std::string& indent, uint64_t limit);
		void Member(const clang::FieldDecl* field, uint64_t at, Cursor& cursor, const std::string& indent, uint64_t limit);
		void AnonymousUnion(const clang::RecordDecl* rec, uint64_t at, Cursor& cursor, const std::string& indent, uint64_t limit, bool inView);
		void Arms(const clang::RecordDecl* rec, const std::string& indent, uint64_t limit);
		void Pad(Cursor& cursor, uint64_t to, const std::string& indent);
		void Bytes(const std::string& name, uint64_t count, const std::string& indent);
		bool Typed(clang::QualType type, uint64_t at, uint64_t limit, std::string& spelling) const;
		bool PlacesItself(clang::QualType type) const;
		uint64_t SizeOf(clang::QualType type) const { return static_cast<uint64_t>(m_Ctx.getTypeSizeInChars(type).getQuantity()); }
		uint64_t AlignOf(clang::QualType type) const { return static_cast<uint64_t>(m_Ctx.getTypeAlignInChars(type).getQuantity()); }
		std::string Fresh(const char* prefix) { return prefix + std::to_string(m_Fresh++); }

		clang::ASTContext& m_Ctx;
		C4TypeMapper& m_Mapper;
		std::string& m_Out;
		int m_Fresh = 0;
	};
} // namespace CBind
