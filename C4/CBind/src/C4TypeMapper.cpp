#include "C4TypeMapper.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Type.h"

namespace CBind
{
	bool C4TypeMapper::Map(clang::QualType t, std::string& out)
	{
		// std::initializer_list<T> -> __init_list<T>. The class template's ABI ({const T*, size_t},
		// 16 bytes, passed by value in registers) is exactly C4's built-in __init_list<T>, so a
		// foreign function taking an initializer list by value can be called from a C4 braced
		// literal. References and pointers to it map to __init_list<T>* (C4 has no references; the
		// caller passes the address of a local list). This must be detected on the NON-canonical
		// type: canonicalization strips the TemplateSpecializationType sugar into a plain
		// RecordType, losing the template identity (including through reference/pointer layers).
		auto mapInitListElem = [this](const clang::TemplateArgument& arg, std::string& elem) -> bool {
			// Canonicalize so typedefs (int32_t -> int) resolve to primitives; MapSingle has no
			// alias handling.
			return MapSingle(m_Ctx.getCanonicalType(arg.getAsType()), true, elem);
		};
		auto tryMapInitList = [this, &mapInitListElem](const clang::QualType& t, std::string& out) -> bool {
			const auto* tst = dyn_cast<clang::TemplateSpecializationType>(t.getTypePtr());
			if (!tst) return false;
			const clang::TemplateDecl* tmplDecl = tst->getTemplateName().getAsTemplateDecl();
			const auto* ns = dyn_cast_or_null<clang::NamespaceDecl>(
				tmplDecl ? tmplDecl->getDeclContext() : nullptr);
			if (!tmplDecl || tmplDecl->getNameAsString() != "initializer_list" ||
				!ns || ns->getNameAsString() != "std")
				return false;
			const auto args = tst->template_arguments();
			if (args.size() != 1) return false;
			std::string elem;
			if (!mapInitListElem(args[0], elem)) return false;
			out = "__init_list<" + elem + ">";
			return true;
		};

		const clang::Type* topTy = t.getTypePtr();
		if (tryMapInitList(t, out))
			return true;
		if (const auto* rft = dyn_cast<clang::ReferenceType>(topTy)) {
			std::string ref;
			if (tryMapInitList(rft->getPointeeType(), ref)) {
				out = ref + "*";
				return true;
			}
		} else if (const auto* ptt = dyn_cast<clang::PointerType>(topTy)) {
			std::string pt;
			if (tryMapInitList(ptt->getPointeeType(), pt)) {
				out = pt + "*";
				return true;
			}
		}

		// If this type is a typedef we bound as a C4 alias, use the alias name so generated
		// signatures read naturally (HWND, DWORD, LPCWSTR) rather than their canonical form.
		// Check the *declared* (non-canonical) type because MapSingle would canonicalize away
		// the typedef. This also sidesteps any pointer-size edge cases for handle types.
		if (const auto* td = dyn_cast<clang::TypedefType>(topTy))
		{
			auto it = m_Aliases.find(td->getDecl()->getNameAsString());
			if (it != m_Aliases.end())
			{
				out = it->second;
				return true;
			}
		}
		return MapSingle(m_Ctx.getCanonicalType(t), true, out);
	}

	// A const pointee in C4 spelling: `const T` for a value, `T const*` for a pointer layer (D-30).
	std::string C4TypeMapper::WithConst(const std::string& type, bool isConst)
	{
		if (!isConst) return type;
		if (!type.empty() && type.back() == '*') return type.substr(0, type.size() - 1) + " const*";
		return "const " + type;
	}

	bool C4TypeMapper::MapSingle(clang::QualType t, bool allowVoid, std::string& out)
	{
		const clang::Type* ty = t.getTypePtr();

		if (const auto* bt = dyn_cast<clang::BuiltinType>(ty))
		{
			switch (bt->getKind())
			{
			case clang::BuiltinType::Void:
				return allowVoid ? (out = "void", true) : false;
			case clang::BuiltinType::Char_S:
			case clang::BuiltinType::Char_U:
				out = "char"; return true;   // plain char is C4's char (char8): C strings stay `char*`
			case clang::BuiltinType::SChar:
				out = "int8"; return true;
			case clang::BuiltinType::UChar:
				out = "uint8"; return true;
			case clang::BuiltinType::WChar_S:
			case clang::BuiltinType::WChar_U:
			case clang::BuiltinType::Char16:
				out = m_Ctx.getTypeSize(t) == 32 ? "char32" : "char16"; return true; // wchar_t is a UTF-16 unit on Windows
			case clang::BuiltinType::Char32:
				out = "char32"; return true;
			case clang::BuiltinType::Char8:
				out = "char"; return true;
			case clang::BuiltinType::Short:
				out = "int16"; return true;
			case clang::BuiltinType::UShort:
				out = "uint16"; return true;
			case clang::BuiltinType::Int:
				out = "int32"; return true;
			case clang::BuiltinType::UInt:
				out = "uint32"; return true;
			case clang::BuiltinType::Long:
				// Windows LLP64: long == 32-bit. Map by size for correctness.
				out = m_Ctx.getTypeSize(t) == 64 ? "int64" : "int32"; return true;
			case clang::BuiltinType::ULong:
				out = m_Ctx.getTypeSize(t) == 64 ? "uint64" : "uint32"; return true;
			case clang::BuiltinType::LongLong:
				out = "int64"; return true;
			case clang::BuiltinType::ULongLong:
				out = "uint64"; return true;
			case clang::BuiltinType::Float:
				out = "float"; return true;   // C4 source name for 32-bit float
			case clang::BuiltinType::Double:
				out = "double"; return true;  // C4 source name for 64-bit float
			default:
				// Size-based fallback for integer/character builtins whose enum name varies by
				// Clang version (WChar_T, Char16, Char32, ...). Map by bit-width so pointers to
				// these stay typed instead of degrading to void*. Guarded on isIntegerType() so
				// exotic non-integer builtins still fall through to the opaque path. All such
				// Windows character types are unsigned in practice; signedness is ABI-irrelevant.
				if (bt->isIntegerType())
				{
					switch (m_Ctx.getTypeSize(t))
					{
						case 8:  out = "uint8"; return true;
						case 16: out = "uint16"; return true;
						case 32: out = "uint32"; return true;
						case 64: out = "uint64"; return true;
					}
				}
				return false;
			}
		}

		if (const auto* pt = dyn_cast<clang::PointerType>(ty))
		{
			const clang::QualType pointeeTy = pt->getPointeeType();
			if (const auto* fpt = dyn_cast_or_null<clang::FunctionProtoType>(pointeeTy.getTypePtr()))
				return MapFunction(fpt, out); // funcptr<R(A,B)>
			std::string pointee;
			// getTypeSize() returns bits; a pointer is 64 bits on x64.
			const bool ptrSizeOk = m_Ctx.getTypeSize(t) == 64;
			if (MapSingle(pointeeTy, true, pointee) && ptrSizeOk)
				out = WithConst(pointee, pointeeTy.isConstQualified()) + "*";
			else
				out = WithConst("void", pointeeTy.isConstQualified()) + "*"; // opaque fallback - still ABI-correct (all pointers are 8 bytes)
			return true;
		}

		// C++ reference: `T&` / `const T&`. On the Windows x64 ABI a reference is passed and
		// returned exactly like an 8-byte pointer to the referent, so mapping it to `T*` is
		// ABI-correct. The generated shim keeps the original C++ reference spelling for its
		// parameter, so the C++ compiler handles lvalue semantics; the C4 caller just passes `&x`.
		if (const auto* rft = dyn_cast<clang::ReferenceType>(ty))
		{
			std::string ref;
			const bool constReferent = rft->getPointeeType().isConstQualified();
			if (MapSingle(rft->getPointeeType(), true, ref))
				out = WithConst(ref, constReferent) + "*";
			else
				out = WithConst("void", constReferent) + "*"; // opaque referent - still an 8-byte address
			return true;
		}

		if (const auto* fpt = dyn_cast<clang::FunctionProtoType>(ty))
			return MapFunction(fpt, out);

	// std::initializer_list<T> -> __init_list<T>. The class template's ABI ({const T*, size_t},
	// 16 bytes, passed by value in registers) is exactly C4's built-in __init_list<T>, so a
	// foreign function taking an initializer list by value can be called from a C4 braced
	// literal. A const reference to it maps through the ReferenceType case above to
	// __init_list<T>* (the caller passes the address of a local list).
	if (const auto* tst = dyn_cast<clang::TemplateSpecializationType>(ty))
	{
		const clang::TemplateDecl* tmplDecl = tst->getTemplateName().getAsTemplateDecl();
		const auto* ns = dyn_cast_or_null<clang::NamespaceDecl>(
			tmplDecl ? tmplDecl->getDeclContext() : nullptr);
		if (tmplDecl && tmplDecl->getNameAsString() == "initializer_list" &&
			ns && ns->getNameAsString() == "std")
		{
			const auto args = tst->template_arguments();
			if (args.size() == 1)
			{
				std::string elem;
				if (MapSingle(args[0].getAsType(), true, elem))
				{
					out = "__init_list<" + elem + ">";
					return true;
				}
			}
		}
	}

		if (const auto* rt = dyn_cast<clang::RecordType>(ty))
		{
			auto it = m_Records.find(rt->getDecl());
			if (it == m_Records.end())
				return false; // anonymous/unregistered record - caller skips
			out = it->second; // C4 references types by bare name (no 'struct' keyword)
			return true;
		}

		if (const auto* et = dyn_cast<clang::EnumType>(ty))
		{
			auto it = m_Enums.find(et->getDecl());
			if (it == m_Enums.end())
				return false;
			out = it->second; // C4 references types by bare name (no 'enum' keyword)
			return true;
		}

		// A SIMD vector (`__m128`, `__m256i`, `float __attribute__((vector_size(32)))`) is C4's packed vector of the
		// same lanes, one register value passed as clang passes it (D-62). C4 holds 2 to 16 lanes in exactly their
		// bytes, so a vector C pads (a 3-lane ext_vector_type is 16 bytes) has no C4 spelling.
		if (const auto* vt = dyn_cast<clang::VectorType>(ty))
		{
			std::string element;
			const unsigned lanes = vt->getNumElements();
			const bool unpadded = m_Ctx.getTypeSize(t) == lanes * m_Ctx.getTypeSize(vt->getElementType());
			if (lanes < 2 || lanes > 16 || !unpadded || !MapSingle(vt->getElementType(), false, element))
				return false;
			out = "packed " + element + "[" + std::to_string(lanes) + "]";
			return true;
		}

		if (const auto* at = dyn_cast<clang::ArrayType>(ty))
		{
			uint64_t count = 0;
			if (auto* sz = dyn_cast_or_null<clang::ConstantArrayType>(at))
				count = sz->getSize().getZExtValue();
			std::string e;
			if (!MapSingle(at->getElementType(), true, e) || count == 0)
				return false;
			out = e + "[" + std::to_string(count) + "]";
			return true;
		}

		// Typedefs are already canonicalized away by Map(); a bare TypedefType here is unexpected.
		return false;
	}

	bool C4TypeMapper::MapFunction(const clang::FunctionProtoType* fpt, std::string& out)
	{
		std::string ret;
		if (!MapSingle(fpt->getReturnType(), true, ret))
			return false;

		std::string params;
		for (auto it = fpt->param_type_begin(); it != fpt->param_type_end(); ++it)
		{
			std::string p;
			if (!MapSingle(*it, false, p))
				return false;
			if (!params.empty())
				params += ", ";
			params += p;
		}
		out = "funcptr<" + ret + "(" + params + ")>";
		return true;
	}
}
