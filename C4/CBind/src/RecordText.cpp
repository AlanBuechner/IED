#include "RecordText.h"
#include "C4Text.h"
#include "clang/AST/RecordLayout.h"
#include <algorithm>

namespace CBind
{
	namespace
	{
		uint64_t AlignUp(uint64_t value, uint64_t align) { return align == 0 ? value : (value + align - 1) / align * align; }

		std::string UnsignedOfSize(uint64_t bytes) { return "uint" + std::to_string(bytes * 8); }

		// The record of a member with no name: an anonymous struct or union, or (a Microsoft extension, used by
		// windows.h) a named struct type embedded without a field name, whose members are its owner's.
		const clang::RecordDecl* AnonymousMember(const clang::FieldDecl* field)
		{
			if (!field->getNameAsString().empty() || field->isBitField()) return nullptr;
			return field->getType()->getAsRecordDecl();
		}
	}

	void RecordText::StructBody(const clang::RecordDecl* rec, uint64_t start)
	{
		const clang::ASTRecordLayout& layout = m_Ctx.getASTRecordLayout(rec);
		Cursor cursor;
		cursor.offset = start;
		Fields(rec, 0, cursor, "\t", static_cast<uint64_t>(layout.getAlignment().getQuantity()), false);
		// C4 rounds the size up to its alignment, which divides clang's: when that already reaches clang's size, no
		// padding is needed.
		const uint64_t size = static_cast<uint64_t>(layout.getSize().getQuantity());
		if (AlignUp(cursor.offset, cursor.align) != size) Pad(cursor, size, "\t");
	}

	void RecordText::UnionBody(const clang::RecordDecl* rec)
	{
		Arms(rec, "\t", static_cast<uint64_t>(m_Ctx.getASTRecordLayout(rec).getAlignment().getQuantity()));
	}

	// A struct's members at `base` within what is being written (an anonymous struct's are its owner's).
	void RecordText::Fields(const clang::RecordDecl* rec, uint64_t base, Cursor& cursor, const std::string& indent, uint64_t limit, bool inView)
	{
		const clang::ASTRecordLayout& layout = m_Ctx.getASTRecordLayout(rec);
		const std::vector<const clang::FieldDecl*> fields(rec->field_begin(), rec->field_end());
		for (size_t i = 0; i < fields.size();)
		{
			const clang::FieldDecl* field = fields[i];
			const uint64_t at = base + layout.getFieldOffset(static_cast<unsigned>(i)) / 8;
			if (field->isBitField())
			{
				size_t end = i;
				while (end < fields.size() && fields[end]->isBitField()) ++end;
				const uint64_t stop = base + (end < fields.size() ? layout.getFieldOffset(static_cast<unsigned>(end)) / 8
					: static_cast<uint64_t>(layout.getDataSize().getQuantity()));
				// The bytes the bits reach, in whole units of the first bitfield's type, never past the next member.
				const uint64_t bitsEnd = base + (layout.getFieldOffset(static_cast<unsigned>(end - 1)) + fields[end - 1]->getBitWidthValue() + 7) / 8;
				const uint64_t unit = SizeOf(field->getType());
				Bitfields({ fields.begin() + i, fields.begin() + end }, at, std::min(stop, at + AlignUp(bitsEnd - at, unit)), cursor, indent, limit);
				i = end;
				continue;
			}
			const clang::RecordDecl* anonymous = AnonymousMember(field);
			if (anonymous && anonymous->isUnion()) AnonymousUnion(anonymous, at, cursor, indent, limit, inView);
			else if (anonymous) Fields(anonymous, at, cursor, indent, limit, inView);
			else Member(field, at, cursor, indent, limit);
			++i;
		}
	}

	// The integers that store a run of bitfields: whole units of the first one's type when they fit, else bytes.
	void RecordText::Bitfields(const std::vector<const clang::FieldDecl*>& run, uint64_t start, uint64_t end, Cursor& cursor,
		const std::string& indent, uint64_t limit)
	{
		std::string name = "__bits";
		for (const clang::FieldDecl* field : run)
			if (!field->getNameAsString().empty()) name += "_" + field->getNameAsString();
		if (name == "__bits") name = Fresh("__bits");
		Pad(cursor, start, indent);
		const uint64_t unit = SizeOf(run.front()->getType());
		const uint64_t span = end - start;
		if (span == 0) return;
		if (unit <= limit && unit <= 8 && start % unit == 0 && span % unit == 0)
		{
			const std::string type = UnsignedOfSize(unit);
			m_Out += "\n" + indent + (span == unit ? type : type + "[" + std::to_string(span / unit) + "]") + " " + name + ";";
		}
		else Bytes(name, span, indent);
		cursor.offset = end;
	}

	void RecordText::Member(const clang::FieldDecl* field, uint64_t at, Cursor& cursor, const std::string& indent, uint64_t limit)
	{
		const clang::QualType type = field->getType();
		if (type->isIncompleteArrayType()) return; // a flexible array member: no size, nothing to place
		const std::string name = field->getNameAsString();
		std::string spelling;
		if (Typed(type, at, limit, spelling))
		{
			if (!PlacesItself(type) || AlignUp(cursor.offset, AlignOf(type)) != at) Pad(cursor, at, indent);
			if (PlacesItself(type)) cursor.align = std::max(cursor.align, AlignOf(type));
			m_Out += "\n" + indent + AccessOf(field->getAccess()) + spelling + " " + name + ";";
		}
		else
		{
			Pad(cursor, at, indent);
			Bytes(name, SizeOf(type), indent);
		}
		cursor.offset = at + SizeOf(type);
	}

	void RecordText::AnonymousUnion(const clang::RecordDecl* rec, uint64_t at, Cursor& cursor, const std::string& indent, uint64_t limit, bool inView)
	{
		const clang::ASTRecordLayout& layout = m_Ctx.getASTRecordLayout(rec);
		const uint64_t size = static_cast<uint64_t>(layout.getSize().getQuantity());
		const uint64_t align = static_cast<uint64_t>(layout.getAlignment().getQuantity());
		Pad(cursor, at, indent);
		if (inView || align > limit || at % align != 0) Bytes(Fresh("__union"), size, indent);
		else
		{
			m_Out += "\n" + indent + "public union {";
			Arms(rec, indent + "\t", limit);
			m_Out += "\n" + indent + "};";
		}
		cursor.offset = at + size;
	}

	// A union's members, each at its offset 0: anonymous unions inside it are flattened into it.
	void RecordText::Arms(const clang::RecordDecl* rec, const std::string& indent, uint64_t limit)
	{
		const uint64_t size = static_cast<uint64_t>(m_Ctx.getASTRecordLayout(rec).getSize().getQuantity());
		uint64_t largest = 0;
		for (const clang::FieldDecl* field : rec->fields())
		{
			Cursor arm;
			const clang::RecordDecl* anonymous = AnonymousMember(field);
			if (field->isBitField()) Bitfields({ field }, 0, SizeOf(field->getType()), arm, indent, limit);
			else if (anonymous && anonymous->isUnion())
			{
				Arms(anonymous, indent, limit);
				arm.offset = static_cast<uint64_t>(m_Ctx.getASTRecordLayout(anonymous).getSize().getQuantity());
			}
			else if (anonymous)
			{
				m_Out += "\n" + indent + "struct {";
				Fields(anonymous, 0, arm, indent + "\t", limit, true);
				Pad(arm, static_cast<uint64_t>(m_Ctx.getASTRecordLayout(anonymous).getSize().getQuantity()), indent + "\t");
				m_Out += "\n" + indent + "};";
			}
			else Member(field, 0, arm, indent, limit);
			largest = std::max(largest, arm.offset);
		}
		if (largest < size) Bytes(Fresh("__bytes"), size, indent);
	}

	void RecordText::Pad(Cursor& cursor, uint64_t to, const std::string& indent)
	{
		if (to <= cursor.offset) return;
		Bytes(Fresh("__pad"), to - cursor.offset, indent);
		cursor.offset = to;
	}

	void RecordText::Bytes(const std::string& name, uint64_t count, const std::string& indent)
	{
		if (count > 0) m_Out += "\n" + indent + "uint8[" + std::to_string(count) + "] " + name + ";";
	}

	// A member C4 can hold as its own type at `at`: spelled, and aligned no more than the record allows.
	bool RecordText::Typed(clang::QualType type, uint64_t at, uint64_t limit, std::string& spelling) const
	{
		const uint64_t align = AlignOf(type);
		return align <= limit && at % align == 0 && m_Mapper.Map(type, spelling);
	}

	// A member C4 aligns exactly as clang does: a number, a pointer, an enum, a vector, or an array of them. A record's
	// C4 alignment can be smaller than clang's (its members may be bytes), so a record is always placed by padding.
	bool RecordText::PlacesItself(clang::QualType type) const
	{
		const clang::Type* element = m_Ctx.getBaseElementType(type).getCanonicalType().getTypePtr();
		return element->isBuiltinType() || element->isPointerType() || element->isEnumeralType() || element->isVectorType();
	}
} // namespace CBind
