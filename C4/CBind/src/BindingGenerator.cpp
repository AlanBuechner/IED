#include "BindingGenerator.h"
#include "C4TypeMapper.h"
#include "C4Text.h"
#include "DeclFacts.h"
#include "RecordText.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
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
	bool BindingGenerator::EmitRecord(const clang::RecordDecl* rec, const std::string& c4Name, C4TypeMapper& mapper, clang::ASTContext& ctx, std::string& out)
	{
		if (!HasLayout(rec))
			return false; // a forward declaration, or a template pattern clang lays out only per instance

		const std::string uuid = GetRecordUuid(rec);
		if (!uuid.empty())
			out += "[uuid(\"" + uuid + "\")]\n";
		RecordText text(ctx, mapper, out);
		out += (rec->isUnion() ? "union " : "struct ") + c4Name + " {";
		if (rec->isUnion()) text.UnionBody(rec);
		else text.StructBody(rec, 0);
		out += "\n};\n";
		return true;
	}

	bool BindingGenerator::EmitEnum(const clang::EnumDecl* en, const std::string& c4Name, std::string& out)
	{
		if (!en->isCompleteDefinition())
			return false;

		out += "enum " + c4Name + " {";
		bool first = true;
		for (auto it = en->enumerator_begin(); it != en->enumerator_end(); ++it)
		{
			const auto* c = *it;
			if (!first) out += ",";
			first = false;
			out += "\n\t" + c->getNameAsString();
			m_Bound.insert(c->getNameAsString());
			llvm::SmallString<32> val;
			c->getInitVal().toString(val, 10);
			out += " = " + std::string(val.str());
		}
		out += "\n};\n";
		return true;
	}

	bool BindingGenerator::EmitFunction(const clang::Decl* decl, clang::ASTContext& ctx, C4TypeMapper& mapper, std::string& out, const std::string& cxxPrefix)
	{
		const auto* fn = cast<clang::FunctionDecl>(decl);
		const FunctionBinding binding = HowToBind(fn, ctx, m_Opts);
		if (binding == FunctionBinding::None)
			return false;
		const auto* proto = dyn_cast<clang::FunctionProtoType>(fn->getType()->getAs<clang::FunctionType>());
		if (!proto)
			return false;

		std::string ret;
		if (!MapReturn(fn, mapper, ret))
			return false;

		std::string params;
		if (!MapParams(fn, mapper, params))
			return false;
		// A variadic function keeps its `...`; a wrapper cannot forward one, so such a function is not bound.
		if (proto->isVariadic())
		{
			if (binding == FunctionBinding::Wrapper) return false;
			params += params.empty() ? "..." : ", ...";
		}

		// Always a [link]: C4 would mangle the declaration's own name. A wrapper's symbol is the shim's extern "C"
		// forwarder (GenerateShim); the symbol stays bare (C4 has no namespaces).
		const std::string symbol = binding == FunctionBinding::Wrapper ? "cbind_fn_" + fn->getNameAsString() : fn->getNameAsString();
		out += "\n[link(\"" + symbol + "\")]\n";

		out += ret + " " + fn->getNameAsString() + "(" + params + ");\n";
		return true;
	}

	bool BindingGenerator::EmitClass(const clang::CXXRecordDecl* rec, const std::string& className,
		C4TypeMapper& mapper, clang::ASTContext& ctx, std::string& out, const std::string& cxxPrefix)
	{
		if (!HasLayout(rec))
			return false; // a forward declaration, or a template pattern clang lays out only per instance

		// 1. Struct with the data members. Layout matches C++ exactly: when this class or any base
		// has virtual functions, C++ places a vptr at offset 0. C4's ObjectType::CalculateLayout
		// reserves that slot for its own [virtual] methods - but generated bindings have no C4
		// virtuals, so the class that INTRODUCES the vptr (the first in the hierarchy to have any
		// virtual function) gets an explicit `void* __vtable` placeholder field; derived classes
		// inherit it through the base clause below and must NOT re-emit it.
		bool emitVptr = HasOwnVirtuals(rec) && !BaseHasVirtuals(rec);

		// Symbol base for this class's extern "C" wrappers. Kept BARE (just the class name) to
		// match GenerateShim and the C4 declaration; C4 has no namespaces, so two same-named
		// classes in different namespaces would already collide in C4's type table regardless.
		// The generated shim still references the real C++ type fully-qualified (cxxPrefix+name).
		const std::string& symBase = className;

		std::string declHead = "struct " + className;
		for (const auto& base : rec->bases())
		{
			const auto* baseRec = base.getType()->getAsCXXRecordDecl();
			if (!baseRec || baseRec->getNameAsString().empty()) continue;
			declHead += " : public " + baseRec->getNameAsString();
		}

		// COM GUID: emit a [uuid("...")] attribute so C4's __uuidof(T) can query the IID.
		// (C4 struct definitions accept leading annotations; the semantic layer stores the
		// parts on the ObjectType and codegen materializes a const _GUID.)
		const std::string uuid = GetRecordUuid(rec);
		if (!uuid.empty())
			out += "[uuid(\"" + uuid + "\")]\n";

		out += declHead + " {";
		if (emitVptr)
			out += "\n\tvoid* __vtable; // C++ vptr placeholder - keeps field offsets aligned with the C++ class";
		// The bases and the vtable pointer come first in C4 as in C++: the class's own fields start where clang's do.
		const clang::ASTRecordLayout& layout = ctx.getASTRecordLayout(rec);
		RecordText(ctx, mapper, out).StructBody(rec, rec->field_empty() ? static_cast<uint64_t>(layout.getSize().getQuantity()) : layout.getFieldOffset(0) / 8);
		EmitStaticData(rec, mapper, ctx, out);
		out += "\n};\n";

		// Collect method groups by name for overload detection. Special members (copy/move
		// assignment, conversion operators) are implicit C++ plumbing - never bindable from C4.
		std::vector<const clang::CXXConstructorDecl*> ctors;
		std::vector<const clang::CXXDestructorDecl*> dtors;
		std::unordered_map<std::string, std::vector<const clang::CXXMethodDecl*>> methods;
		for (const auto* m : rec->methods())
		{
			if (const auto* ctor = dyn_cast<clang::CXXConstructorDecl>(m))
			{
				if (!ctor->isDeleted() && !ctor->isDefaulted())
					ctors.push_back(ctor);
			}
			else if (const auto* dtor = dyn_cast<clang::CXXDestructorDecl>(m))
			{
				if (BindsDestructor(rec, dtor))
					dtors.push_back(dtor);
			}
			else
			{
				const std::string mname = m->getNameAsString();
			if (mname.rfind("operator", 0) == 0)
				continue; // operator=, operator==, conversions - not C4-bindable
			methods[mname].push_back(m);
		}
	}

	// No user-defined constructor: bind the implicit/defaulted one so `new T()` runs real C++
		// initialization (a bare aggregate-init would skip it and could clobber a vptr slot).
		if (ctors.empty())
		{
			for (const auto* m : rec->methods())
			{
				const auto* ctor = dyn_cast<clang::CXXConstructorDecl>(m);
				if (ctor && !ctor->isDeleted() && ctor->getNumParams() == 0)
				{
					out += "\n[operator(\"create\")]\n";
					out += "[link(\"" + WrapperName(symBase, "Construct") + "\")]\n";
					out += "void Construct() extends " + className + ";\n";
					break;
				}
			}
		}

		auto emitMethod = [&](const clang::CXXMethodDecl* m, const std::string& c4Name, const std::string& symbolName) {
			std::string ret;
			if (!MapReturn(m, mapper, ret)) return;
			std::string params;
			if (!MapParams(m, mapper, params)) return;

			bool isStatic = m->isStatic();

			if (isStatic)
			{
				// Static method: no `this`. Bound as a plain free function prefixed with the class
				// name so it stays namespaced and collision-free. The [link] annotation names the
				// extern "C" C++ wrapper in the shim file (see GenerateShim), which forwards to the
				// real static method. Called as ClassName_Method(args).
				out += "\n[link(\"" + WrapperName(symBase, "st_" + symbolName) + "\")]\n";
				out += ret + " " + className + "_" + c4Name + "(" + params + ");\n";
				return;
			}

			// Instance (and virtual) method -> extension method. C4 injects `this` as the first
			// parameter and the call site passes the object pointer FIRST, matching C++'s
			// hidden-this ABI exactly. The [link] annotation names the extern "C" C++ wrapper in
			// the shim file; for virtual methods that wrapper calls through a typed pointer, so
			// the C++ compiler performs real vtable dispatch (polymorphic).
			out += "[link(\"" + WrapperName(symBase, symbolName) + "\")]\n";
			out += ret + " " + c4Name + "(" + params + ")" + (m->isConst() ? " const" : "") + " extends " + className + ";\n";
		};

		// 2. Constructors -> [operator("create")] extension methods (declarations only). `new T(...)`
		// allocates a C4-managed block and calls the bound ctor wrapper with the payload pointer
		// as `this`; the wrapper placement-news the real object at that address.
		for (size_t i = 0; i < ctors.size(); ++i)
		{
			std::string params;
			if (!MapParams(ctors[i], mapper, params)) continue;
			out += "\n[operator(\"create\")]\n";
			out += "[link(\"" + WrapperName(symBase, OverloadSymbol("Construct", i, ctors.size())) + "\")]\n";
			out += "void Construct(" + params + ") extends " + className + ";\n";
		}

		// 3. Destructor -> [operator("delete")] extension method; scope exit, delete and the GC run it exactly
		// once when the object is reclaimed (or explicitly freed). The wrapper calls the real dtor.
		for (const auto* dtor : dtors)
		{
			out += "\n[operator(\"delete\")]\n";
			out += "[link(\"" + WrapperName(symBase, "Finalize") + "\")]\n";
			out += "void Finalize() extends " + className + ";\n";
		}

		// 4. Regular methods -> extension methods under their C++ names; overload resolution picks among them.
		for (auto& [name, group] : methods)
			for (size_t i = 0; i < group.size(); ++i)
				emitMethod(group[i], name, OverloadSymbol(name, i, group.size()));

		return true;
	}

	std::string BindingGenerator::Generate(clang::ASTContext& ctx)
	{
		C4TypeMapper mapper(ctx);
		const auto* tu = ctx.getTranslationUnitDecl();

		// Bind the FULL translation unit, not just the named header. Real system APIs spread
		// their types and functions across many transitive headers (IUnknown, GUID, POINT, base
		// COM interfaces, DXGI structs, ...), so restricting to the main file would miss them.
		// C4's name space is flat, so the same C4 name can arrive from different decls; those
		// collisions are resolved first-wins in source order - deterministic and stable across
		// builds (the Clang AST visit order for a given TU is fixed).
		std::unordered_map<std::string, const clang::TagDecl*> tagByName; // first wins
		auto claimTag = [&](const clang::TagDecl* tag, const std::string& name) {
			if (!tag || name.empty()) return;
			if (!tagByName.count(name)) tagByName[name] = tag;
		};

		WalkDecls(tu, "", [&](const clang::Decl* d, const std::string&) {
			if (const auto* rec = dyn_cast<clang::RecordDecl>(d))
			{
				if (rec->isThisDeclarationADefinition())
					claimTag(rec, rec->getNameAsString());
			}
			else if (const auto* en = dyn_cast<clang::EnumDecl>(d))
				claimTag(en, en->getNameAsString());
			else if (const auto* td = dyn_cast<clang::TypedefNameDecl>(d))
			{
				const clang::TagDecl* tag = UnderlyingTag(td);
				if (tag && tag->getNameAsString().empty()) // anonymous struct/enum typedef
					claimTag(tag, td->getNameAsString());
			}
		});
		// Record types fields need that no file-scope declaration names (BUG-280): a struct defined inside another (C
		// puts it at file scope, clang inside its owner) keeps its name, an anonymous one is named after its owner and
		// field (`in_addr_S_un`). Each is written just before its owner.
		std::unordered_set<const clang::TagDecl*> claimed;
		for (auto& [name, tag] : tagByName) claimed.insert(tag);
		std::unordered_map<const clang::TagDecl*, std::vector<const clang::RecordDecl*>> innerTypes;
		std::function<void(const clang::RecordDecl*, const clang::RecordDecl*, const std::string&)> claimFieldTypes =
			[&](const clang::RecordDecl* owner, const clang::RecordDecl* rec, const std::string& prefix) {
				for (const auto* field : rec->fields())
				{
					const clang::RecordDecl* inner = ctx.getBaseElementType(field->getType())->getAsRecordDecl();
					if (!inner || !HasLayout(inner)) continue;
					if (field->getNameAsString().empty())
					{
						claimFieldTypes(owner, inner, prefix);
						continue;
					}
					const std::string name = inner->getNameAsString().empty() ? prefix + "_" + field->getNameAsString() : inner->getNameAsString();
					if (claimed.count(inner) || tagByName.count(name)) continue;
					claimTag(inner, name);
					claimed.insert(inner);
					innerTypes[owner].push_back(inner);
					claimFieldTypes(inner, inner, name);
				}
			};
		WalkDecls(tu, "", [&](const clang::Decl* d, const std::string&) {
			const clang::RecordDecl* rec = dyn_cast<clang::RecordDecl>(d);
			if (const auto* td = dyn_cast<clang::TypedefNameDecl>(d)) rec = dyn_cast_or_null<clang::RecordDecl>(UnderlyingTag(td));
			if (!rec || !rec->isCompleteDefinition()) return;
			for (auto& [name, tag] : tagByName)
				if (tag == rec) return claimFieldTypes(rec, rec, name);
		});

		std::unordered_map<const clang::TagDecl*, std::string> tagNames;
		for (auto& [name, tag] : tagByName)
		{
			// Skip COM vtable helper structs (`...Vtbl`) entirely: not registered for mapping
			// (so any `XxxVtbl*` reference degrades to an opaque void*) and not emitted. See the
			// circular-layout rationale at the emission site.
			if (name.size() >= 4 && name.compare(name.size() - 4, 4, "Vtbl") == 0)
				continue;
			tagNames[tag] = name;
			if (const auto* rec = dyn_cast<clang::RecordDecl>(tag)) mapper.RegisterRecord(rec, name);
			else if (const auto* en = dyn_cast<clang::EnumDecl>(tag)) mapper.RegisterEnum(en, name);
		}

		std::string out;
		out += "// Auto-generated C4 bindings - do not edit.\n";
		std::unordered_set<std::string> emittedTypes;

		// Pass 2: emit struct/enum/class definitions in source order (descending into namespaces).
		WalkDecls(tu, "", [&](const clang::Decl* d, const std::string&) {
			const clang::TagDecl* tag = nullptr;
			if (const auto* rec = dyn_cast<clang::RecordDecl>(d))
			{
				if (rec->isThisDeclarationADefinition() && !rec->getNameAsString().empty())
					tag = rec;
			}
			else if (const auto* en = dyn_cast<clang::EnumDecl>(d))
			{
				if (!en->getNameAsString().empty())
					tag = en;
			}
			else if (const auto* td = dyn_cast<clang::TypedefNameDecl>(d))
			{
				const clang::TagDecl* t = UnderlyingTag(td);
				if (t && t->getNameAsString().empty()) // anonymous - emit here under the typedef name
					tag = t;
			}
			if (!tag) return;
			auto it = tagNames.find(tag);
			if (it == tagNames.end()) return; // not the first-wins winner for its C4 name
			const std::string& name = it->second;
			// Skip COM vtable helper structs (`...Vtbl`). In C mode each interface is a flat
			// struct holding a `lpVtbl` pointer to a Vtbl struct of function pointers, and
			// those funcptr signatures reference the interfaces themselves - creating circular
			// type layouts (interface -> Vtbl -> interface) that make C4's size computation
			// recurse infinitely. The Vtbl is pure COM plumbing the game never touches; the
			// interface only needs its single 8-byte `lpVtbl` pointer, which degrades to an
			// opaque `void*` once the Vtbl struct itself is not emitted.
			if (name.size() >= 4 && name.compare(name.size() - 4, 4, "Vtbl") == 0)
				return;
			if (emittedTypes.count(name)) return; // this C4 name was already emitted
			emittedTypes.insert(name);

			std::function<void(const clang::TagDecl*)> emitInner = [&](const clang::TagDecl* owner) {
				for (const clang::RecordDecl* inner : innerTypes[owner])
				{
					if (!emittedTypes.insert(tagNames[inner]).second) continue;
					emitInner(inner);
					EmitRecord(inner, tagNames[inner], mapper, ctx, out);
				}
			};
			emitInner(tag);
			if (const auto* rec = dyn_cast<clang::RecordDecl>(tag))
			{
				if (IsCxx(m_Opts) && isa<clang::CXXRecordDecl>(rec))
					EmitClass(static_cast<const clang::CXXRecordDecl*>(rec), name, mapper, ctx, out);
				else
					EmitRecord(rec, name, mapper, ctx, out);
			}
			else if (const auto* en = dyn_cast<clang::EnumDecl>(tag))
				EmitEnum(en, name, out);
		});

		// Pass 2b: emit typedef aliases (`alias Name = <concrete type>;`) so the generated
		// signatures read with their natural names (HWND, DWORD, LPCWSTR) instead of collapsing
		// to void*/int32. Targets are computed via canonical mapping (primitives, void*, tagX,
		// or pointer/array/funcptr of those) - never another alias name - so there is no
		// forward-reference ordering problem: every concrete target is a primitive, an already-
		// emitted struct/enum, or a built-in. First-wins by C4 name; skip names already taken by
		// a struct/enum above or by a C4 built-in type so we never emit a duplicate symbol.
		static const std::unordered_set<std::string> kC4Builtins = {
			"void", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64",
			"float", "double", "char", "bool", "string", "String", "byte", "u8", "u16", "i8", "i16", "i32", "i64"
		};
		std::unordered_set<std::string> aliasSeen;
		WalkDecls(tu, "", [&](const clang::Decl* d, const std::string&) {
			const auto* td = dyn_cast<clang::TypedefNameDecl>(d);
			if (!td) return;
			std::string name = td->getNameAsString();
			if (name.empty() || aliasSeen.count(name) || emittedTypes.count(name) || kC4Builtins.count(name))
				return;
			// Only bind typedefs whose underlying type we can represent concretely.
			std::string target;
			if (!mapper.Map(ctx.getCanonicalType(td->getUnderlyingType()), target))
				return;
			aliasSeen.insert(name);
			out += "alias " + name + " = " + target + ";\n";
			mapper.RegisterAlias(name, name); // function signatures referencing this typedef use its name
		});

		// Pass 3: emit function prototypes (free functions only - methods are emitted by EmitClass).
		// First-wins by bare C4 name keeps this consistent with GenerateShim's wrapper dedup.
		std::unordered_set<std::string> emittedFns;
		WalkDecls(tu, "", [&](const clang::Decl* d, const std::string& prefix) {
			if (isa<clang::FunctionDecl>(d) && !isa<clang::CXXMethodDecl>(d))
			{
				const auto* fn = cast<clang::FunctionDecl>(d);
				std::string name = fn->getNameAsString();
				if (!IsBindableFreeFunctionName(name) || emittedFns.count(name)) return;
				emittedFns.insert(name);
				EmitFunction(d, ctx, mapper, out, prefix);
			}
		});

		m_Bound.insert(emittedTypes.begin(), emittedTypes.end());
		m_Bound.insert(aliasSeen.begin(), aliasSeen.end());
		m_Bound.insert(emittedFns.begin(), emittedFns.end());
		return out;
	}
}
