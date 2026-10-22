#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/CodeGen/CensusTypes.h"
#include "Backend/CodeGen/DebugInfo.h"
#include "Backend/CodeGen/NormalizedConversion.h"
#include "Backend/CodeGen/EhTables.h"
#include "Backend/CodeGen/RttiEmitter.h"
#include "Backend/CodeGen/StableNames.h"
#include "Backend/CodeGen/VtableEmitter.h"
#include "Compiler/ModulePartitions.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FunPtrType.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
// IntegerType / PointerType / VectorType live in DerivedTypes.h on this vendored LLVM 22; the constant
// builders (ConstantInt / ConstantFP) live in Constants.h.
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/TargetParser/Triple.h>
#include <algorithm>

namespace Scripting
{
	namespace
	{
		constexpr const char* kTargetTriple = "x86_64-pc-windows-msvc";
		constexpr const char* kTargetDataLayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
	}

	ModuleContext::ModuleContext(llvm::LLVMContext& ctx)
		: m_Context(ctx), m_Abi(ctx), m_AliasTags(ctx, [this](const Type* t) { return m_Vtables->HasVptrs(t); })
	{
		m_OwnedModule = std::make_unique<llvm::Module>("c4_program", m_Context);
		m_Module = m_OwnedModule.get();
		// The Windows x64 target and the layout clang uses for it, so our struct sizes and aggregate passing
		// agree with C/C++ at every boundary (doc 04).
		m_Module->setTargetTriple(llvm::Triple(kTargetTriple));
		m_DataLayout = llvm::DataLayout(kTargetDataLayout);
		m_Module->setDataLayout(m_DataLayout);
		m_Rtti = std::make_unique<RttiEmitter>(*m_Module);
		m_Vtables = std::make_unique<VtableEmitter>(*this, *m_Rtti);
		m_Eh = std::make_unique<EhTables>(*m_Module, *m_Rtti, [this](const FunctionRecord& rec) { return GetOrCreateFunction(rec); });
		m_Census = std::make_unique<CensusTypes>(*this);
	}

	ModuleContext::~ModuleContext() = default;

	Abi::Signature ModuleContext::SignatureOf(const FunctionRecord& rec) const
	{
		const Type* ret = rec.type ? rec.type->GetReturnType() : nullptr;
		Abi::CallKind kind = Abi::CallKind::Free;
		if ((rec.containingType || rec.primitiveReceiverType) && !rec.isStatic)
			kind = rec.isExtensionMethod || rec.primitiveReceiverType ? Abi::CallKind::Extension : Abi::CallKind::Member;
		return m_Abi.SignatureOf(ret, kind);
	}

	llvm::Type* ModuleContext::Lower(const Type* t)
	{
		if (!t) return llvm::Type::getVoidTy(m_Context);
		auto it = m_TypeCache.find(t);
		if (it != m_TypeCache.end()) return it->second;

		// Alias unwraps to its target before mapping (aliases are transparent everywhere). References do NOT
		// unwrap here: a T& is an address, and it must lower to a pointer both in the storage (the slot holds
		// the pointee's address) and across call boundaries - so Reference maps to ptr in the switch below.
		const Type* base = t;
		while (base && base->GetKind() == TypeKind::Alias)
			base = static_cast<const AliasType*>(base)->GetAliasedType();
		if (!base) return llvm::Type::getVoidTy(m_Context);

		llvm::Type* result = nullptr;
		switch (base->GetKind())
		{
		case TypeKind::Void:   result = llvm::Type::getVoidTy(m_Context); break;
		case TypeKind::Bool:   result = llvm::Type::getInt1Ty(m_Context); break;
		case TypeKind::Int8: case TypeKind::UInt8: case TypeKind::SNorm8: case TypeKind::UNorm8:
		case TypeKind::Char8:   result = llvm::Type::getInt8Ty(m_Context); break;
		case TypeKind::Int16: case TypeKind::UInt16: case TypeKind::SNorm16: case TypeKind::UNorm16:
		case TypeKind::Char16:   result = llvm::Type::getInt16Ty(m_Context); break;
		case TypeKind::Int32: case TypeKind::UInt32: case TypeKind::SNorm32: case TypeKind::UNorm32:
		case TypeKind::Char32:   result = llvm::Type::getInt32Ty(m_Context); break;
		case TypeKind::Int64: case TypeKind::UInt64: case TypeKind::SNorm64: case TypeKind::UNorm64:
			result = llvm::Type::getInt64Ty(m_Context); break;
		case TypeKind::Float32: result = llvm::Type::getFloatTy(m_Context); break;
		case TypeKind::Float64: result = llvm::Type::getDoubleTy(m_Context); break;
		case TypeKind::Pointer: // opaque pointers on LLVM 22
		case TypeKind::Reference: // T& is an address - it lowers to a pointer (the pointee's address)
		case TypeKind::FunPtr:
		case TypeKind::Function:
			result = llvm::PointerType::get(m_Context, 0); break;
		case TypeKind::Array:
		{
			const ArrayType* a = static_cast<const ArrayType*>(base);
			uint64 n = a->GetElementCount();
			llvm::Type* elem = Storage(a->GetElementType());
			result = llvm::ArrayType::get(elem, n ? n : 1); break;
		}
		case TypeKind::Enum:
		{
			const EnumType* e = static_cast<const EnumType*>(base);
			result = Lower(e->GetUnderlyingType() ? e->GetUnderlyingType() : nullptr);
			if (!result) result = llvm::Type::getInt32Ty(m_Context); break;
		}
		case TypeKind::Swizzle:
		{
			const SwizzleType* s = static_cast<const SwizzleType*>(base);
			uint32 lanes = s->GetLaneCount();
			llvm::Type* elem = Lower(s->GetElementType());
			result = llvm::FixedVectorType::get(elem, lanes ? lanes : 1); break;
		}
		case TypeKind::Object:
		{
			const ObjectType* obj = static_cast<const ObjectType*>(base);
			if (obj->IsPurePackedVector()) { // a pure-packed object IS its SIMD vector (passes in XMM regs)
				const SwizzleType* s = obj->GetPackedVectorSwizzle();
				uint32 lanes = s ? s->GetLaneCount() : 1;
				llvm::Type* elem = s ? Lower(s->GetElementType()) : llvm::Type::getFloatTy(m_Context);
				result = llvm::FixedVectorType::get(elem, lanes ? lanes : 1);
			}
			else
			{
				result = StructFor(obj, base->GetName().c_str());
				// Lazily fill the struct body. CodeGenDriver pre-fills every PROGRAM-level type, but a type
				// declared INSIDE a function body (doc 06) is registered only during lowering and is never in
				// model.ObjectTypes() - without this its LLVM struct stays opaque and the slot alloca fails
				// with "Cannot allocate unsized type". StructFor cached the opaque struct first, so a
				// recursive Lower of an in-progress type hits that cache (no infinite loop).
				FillStruct(obj);
			}
			break;
		}
		case TypeKind::Union:
		{
			// A named union is one contiguous byte blob whose members ALL alias at offset 0 - NOT a
			// multi-field struct (that would give each member its own slot and double the size). Represent
			// it as [N x i8] so byte-offset GEP access (every member at offset 0) and by-value copies work.
			const UnionType* un = static_cast<const UnionType*>(base);
			size_t sz = un->GetSize();
			if (sz == 0) sz = 1; // an empty union still needs one byte of storage
			result = llvm::ArrayType::get(llvm::Type::getInt8Ty(m_Context), sz);
			break;
		}
		case TypeKind::InitList:
		{
			// __init_list<T> is the std::initializer_list ABI: a 16-byte aggregate { const T* data, uint64 size }.
			// It lowers to that two-field struct so field GEPs (data @0, size @8) and by-value copies are correct.
			result = llvm::StructType::get(m_Context,
				{ llvm::PointerType::get(m_Context, 0), llvm::Type::getInt64Ty(m_Context) });
			break;
		}
		case TypeKind::Func: // { code, environment } (docs/language/07)
			result = llvm::StructType::get(m_Context, { llvm::PointerType::get(m_Context, 0), llvm::PointerType::get(m_Context, 0) });
			break;
		default:
			result = llvm::Type::getInt8Ty(m_Context); break; // TemplateParam/Invalid: never concrete in HIR
		}

		m_TypeCache[t] = result;
		return result;
	}

	llvm::Type* ModuleContext::Storage(const Type* t)
	{
		llvm::Type* lowered = Lower(t);
		auto* vector = llvm::dyn_cast<llvm::FixedVectorType>(lowered);
		if (!vector || !t || m_DataLayout.getTypeAllocSize(vector).getFixedValue() == TypeHandle::UnwrapToValue(t)->GetSize()) return lowered;
		return llvm::ArrayType::get(vector->getElementType(), vector->getNumElements());
	}

	llvm::StructType* ModuleContext::StructFor(const ObjectType* obj, const char* name)
	{
		// Opaque at creation so recursive and incomplete types are legal; GlobalEmitter fills it.
		auto key = static_cast<const Type*>(obj);
		auto it = m_TypeCache.find(key);
		if (it != m_TypeCache.end()) return llvm::cast<llvm::StructType>(it->second);
		llvm::StructType* st = llvm::StructType::create(m_Context, name);
		m_TypeCache[key] = st;
		return st;
	}

	llvm::FunctionType* ModuleContext::FunctionTypeOf(const FunctionRecord& rec)
	{
		const FunctionType* ft = rec.type;
		const Type* ret = ft ? ft->GetReturnType() : nullptr;

		// Hidden parameters in MSVC x64 order (`this`, then the sret pointer, for an instance method), then the
		// declared parameters per the Abi brick.
		const Abi::Signature sig = SignatureOf(rec);
		auto* ptr = llvm::PointerType::get(m_Context, 0);
		std::vector<llvm::Type*> paramTys(sig.firstParam, ptr);
		if (ft)
			for (uint32 i = 0; i < ft->GetParameterCount(); ++i)
			{
				const Type* p = ft->GetParameters()[i].m_Type;
				llvm::Type* pt = m_Abi.ParamType(p); // aggregate special case, or null for scalars
				if (!pt && rec.isLinked && p && p->GetKind() == TypeKind::InitList)
					pt = ptr; // MSVC passes std::initializer_list by hidden pointer (see EmitCall)
				paramTys.push_back(pt ? pt : Lower(p));
			}

		// Return type: void for sret, the register int for a 1/2/4/8-byte aggregate return, else the lowered
		// storage type (scalars, references-as-ptr, pointers).
		llvm::Type* retTy = sig.sret ? llvm::Type::getVoidTy(m_Context) : m_Abi.ReturnType(ret);
		if (!retTy) retTy = Lower(ret);
		return llvm::FunctionType::get(retTy, paramTys, ft ? ft->IsVariadic() : false);
	}

	// Whether a declaration of the function says `inline` (BUG-357): any of them, so a header's declaration and an
	// out-of-line definition each may.
	static bool DeclaredInline(const FunctionRecord& rec)
	{
		for (const AstNode* declaration : rec.declarations)
			if (const auto* function = dynamic_cast<const FunctionDefinitionNode*>(declaration); function && function->IsInline()) return true;
		return false;
	}

	llvm::Function* ModuleContext::GetOrCreateFunction(const FunctionRecord& rec)
	{
		auto it = m_FuncCache.find(&rec);
		if (it != m_FuncCache.end()) return it->second;

		llvm::FunctionType* fty = FunctionTypeOf(rec);
		// Declarations of one foreign symbol (a builtin and a [link] of the same C function) are one function.
		if (!HasBody(rec))
			if (llvm::Function* existing = m_Module->getFunction(rec.mangledName.c_str()); existing && existing->getFunctionType() == fty)
			{
				m_FuncCache[&rec] = existing;
				return existing;
			}
		// A file-scope `static` free function has internal linkage (doc 09 D4): its symbol is visible only in
		// the unit that defines it. The mangled name already carries a per-unit suffix (FreeFunctionResolver) so
		// same-named statics in different files are distinct; internal linkage then keeps each one from leaking
		// into the exported symbol table and matches C's `static` semantics exactly.
		bool fileStatic = rec.isStatic && !rec.containingType && !rec.primitiveReceiverType &&
			rec.scope && rec.scope->HoldsFileStatics();
		llvm::Function* fn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
			fileStatic ? LocalName(rec.mangledName.c_str()) : std::string(rec.mangledName.c_str()), m_Module);
		if (fileStatic) MakeLocal(*fn);
		if (DeclaredInline(rec)) fn->addFnAttr(llvm::Attribute::AlwaysInline);
		m_FuncCache[&rec] = fn;
		m_NewFunctions.push_back(&rec);
		return fn;
	}

	llvm::Function* ModuleContext::FirstUseStart(const SymbolInfo& sym)
	{
		if (!m_StartsOnFirstUse || !sym.isThreadLocal || m_StartsWithThread.count(&sym)) return nullptr;
		if (auto known = m_FirstUseStarts.find(&sym); known != m_FirstUseStarts.end()) return known->second;
		if (!m_StartsOnFirstUse(sym))
		{
			m_StartsWithThread.insert(&sym);
			return nullptr;
		}
		const std::string name = "__c4_tls_start_" + GlobalAddress(sym)->getName().str();
		auto* start = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(m_Context), false),
			llvm::Function::ExternalLinkage, name, m_Module);
		m_FirstUseStarts[&sym] = start;
		return start;
	}

	llvm::GlobalVariable* ModuleContext::GlobalAddress(const SymbolInfo& sym)
	{
		auto it = m_GlobalCache.find(&sym);
		if (it != m_GlobalCache.end()) return it->second;

		// A [link("sym")] variable aliases a foreign symbol: emit an EXTERNAL declaration named exactly `sym`
		// and never allocate our own storage. The interop pass compiles the extern "c" source that defines it,
		// so linking resolves the reference (doc 04).
		if (!sym.linkSymbol.empty())
		{
			llvm::Type* ty = Lower(sym.type);
			if (llvm::GlobalVariable* existing = m_Module->getGlobalVariable(sym.linkSymbol.c_str()); existing && existing->getValueType() == ty)
			{
				m_GlobalCache[&sym] = existing;
				return existing;
			}
			auto* gv = new llvm::GlobalVariable(*m_Module, ty, false,
				llvm::GlobalValue::ExternalLinkage, nullptr, sym.linkSymbol.c_str());
			gv->setThreadLocal(sym.isThreadLocal);
			m_GlobalCache[&sym] = gv;
			return gv;
		}

		if (m_DefinesGlobal && !m_DefinesGlobal(sym)) return DeclareGlobal(sym);

		// One GlobalVariable per model global, named by the model's canonical symbol (doc 04 interop: the
		// name is what links against foreign objects and what __c4_gc_roots points at). A global whose constant
		// initializer folded to a value (const OR non-const) starts with that value; anything else starts zero.
		llvm::Type* ty = Lower(sym.type);
		llvm::Constant* init = nullptr;
		if (sym.constValue.kind == ConstantValue::Kind::String) init = StringGlobal(sym.constValue);
		else if (!sym.constValue.IsNone() && sym.constValue.kind != ConstantValue::Kind::Guid) init = NumberConstant(sym.constValue, sym.type, ty);
		// A global function pointer initialized with a function address (`funcptr<R(a)> g = F;`) must start
		// at @F, not null - the first call through it would otherwise fault. The initializer names the
		// function (a bare or qualified identifier); resolve it against the model to its record.
		if (!init && m_Model && sym.initializer)
		{
			const Type* base = sym.type ? TypeHandle::UnwrapToValue(sym.type) : nullptr;
			if (base && base->GetKind() == TypeKind::FunPtr)
			{
				Core::String fnName;
				if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(sym.initializer))
					fnName = id->GetName();
				else if (const auto* qn = dynamic_cast<const NamespaceQualifiedNameNode*>(sym.initializer))
					for (uint32 i = 0; i < qn->SegmentCount(); ++i) { if (i) fnName += "::"; fnName += qn->GetSegments()[i].nameToken.GetText(); }
				if (!fnName.empty())
				{
					const auto* fp = static_cast<const FunPtrType*>(base);
					Core::Vector<const Type*> ptypes;
					for (const auto* pt : fp->GetParameterTypes()) ptypes.Push(pt);
					const FunctionRecord* rec = nullptr;
					std::unique_lock<std::mutex> shared = m_ModelLock ? std::unique_lock<std::mutex>(*m_ModelLock) : std::unique_lock<std::mutex>();
					auto sigR = m_Model->QueryFunction(fnName, nullptr, nullptr, ptypes,
						LookupContext{ sym.definingUnit, nullptr, nullptr }, &rec);
					shared = {};
					if (sigR.Ok() && rec) init = GetOrCreateFunction(*rec);
				}
			}
		}
		// A polymorphic global starts with its vptrs set: its initial bytes are an image of the zeroed object.
		if (!init) init = m_Vtables->ZeroImage(sym.type);
		if (!init) init = llvm::Constant::getNullValue(ty);
		auto* gv = new llvm::GlobalVariable(*m_Module, init->getType(), sym.IsFoldedConstant(), llvm::GlobalValue::ExternalLinkage,
			init, GlobalName(sym));
		if (sym.isStatic) MakeLocal(*gv);
		gv->setThreadLocal(sym.isThreadLocal);
		if (sym.type && sym.type->GetAlignment() > 1) gv->setAlignment(llvm::Align(sym.type->GetAlignment()));
		m_GlobalCache[&sym] = gv;
		return gv;
	}

	std::string ModuleContext::GlobalName(const SymbolInfo& sym) const
	{
		const std::string name = sym.mangledName.empty() ? "c4_global" : sym.mangledName.c_str();
		return sym.isStatic ? LocalName(name) : name;
	}

	// The storage another partition defines.
	llvm::GlobalVariable* ModuleContext::DeclareGlobal(const SymbolInfo& sym)
	{
		auto* gv = new llvm::GlobalVariable(*m_Module, Lower(sym.type), sym.IsFoldedConstant(), llvm::GlobalValue::ExternalLinkage,
			nullptr, GlobalName(sym));
		if (sym.isStatic) gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
		gv->setThreadLocal(sym.isThreadLocal);
		if (sym.type && sym.type->GetAlignment() > 1) gv->setAlignment(llvm::Align(sym.type->GetAlignment()));
		m_GlobalCache[&sym] = gv;
		return gv;
	}

	const FunctionType* ModuleContext::CensusCounterType()
	{
		std::unique_lock<std::mutex> shared = m_ModelLock ? std::unique_lock<std::mutex>(*m_ModelLock) : std::unique_lock<std::mutex>();
		TypeStore* types = m_Model->TypeStorePtr();
		return types->Function(types->Builtin(TypeKind::Int64), { types->Pointer(types->Builtin(TypeKind::Void)) });
	}

	void ModuleContext::SetPartition(std::function<bool(const SymbolInfo&)> definesGlobal)
	{
		m_Split = true;
		m_DefinesGlobal = std::move(definesGlobal);
	}

	void ModuleContext::MakeLocal(llvm::GlobalValue& value) const
	{
		if (!m_Split)
		{
			value.setLinkage(llvm::GlobalValue::InternalLinkage);
			return;
		}
		value.setLinkage(llvm::GlobalValue::ExternalLinkage);
		value.setVisibility(llvm::GlobalValue::HiddenVisibility);
	}

	std::string ModuleContext::LocalName(const std::string& name) const
	{
		return m_Split ? name + kLocalSuffix : name;
	}

	void ModuleContext::DeclareStaticStorage(const std::string& name, const Type* type, bool threadLocal)
	{
		llvm::GlobalVariable* storage = DeclaredLocal(name, Lower(type), threadLocal);
		if (type && type->GetAlignment() > 1) storage->setAlignment(llvm::Align(type->GetAlignment()));
		NoteStaticStorage(storage, type);
	}

	llvm::GlobalVariable* ModuleContext::DeclaredLocal(const std::string& name, llvm::Type* type, bool threadLocal)
	{
		if (llvm::GlobalVariable* known = m_Module->getNamedGlobal(name)) return known; // a copy of its function declared it
		auto* local = new llvm::GlobalVariable(*m_Module, type, false, llvm::GlobalValue::ExternalLinkage, nullptr, name);
		local->setVisibility(llvm::GlobalValue::HiddenVisibility);
		local->setThreadLocal(threadLocal);
		return local;
	}

	std::unordered_map<const llvm::Function*, const FunctionRecord*> ModuleContext::RecordsOfFunctions() const
	{
		std::unordered_map<const llvm::Function*, const FunctionRecord*> records;
		for (const auto& [record, function] : m_FuncCache) records.emplace(function, record);
		return records;
	}

	void ModuleContext::FillStruct(const ObjectType* obj)
	{
		if (obj->IsPurePackedVector()) return; // a pure packed vector IS its SIMD <N x T>; it has no struct body
		llvm::StructType* st = StructFor(obj, obj->GetName().c_str());
		if (!st->isOpaque()) return; // already filled - a struct is opaque until its first FillStruct call

		// Field and base ACCESS uses byte-offset GEPs, so the body only has to reproduce the model size and
		// alignment: each piece goes at its exact model offset with i8 padding between, then tail padding.
		std::vector<StructPiece> pieces = StructPieces(obj);
		std::stable_sort(pieces.begin(), pieces.end(), [](const StructPiece& a, const StructPiece& b) { return a.offset < b.offset; });
		std::vector<llvm::Type*> members;
		size_t cursor = 0;
		auto* i8 = llvm::Type::getInt8Ty(m_Context);
		for (const StructPiece& piece : pieces)
		{
			if (piece.offset < cursor) continue; // aliases bytes an earlier piece already covers
			while (cursor < piece.offset) { members.push_back(i8); cursor++; }
			members.push_back(piece.type);
			cursor += piece.size;
		}
		while (cursor < obj->GetSize()) { members.push_back(i8); cursor++; }
		st->setBody(members);
	}

	std::vector<ModuleContext::StructPiece> ModuleContext::StructPieces(const ObjectType* obj)
	{
		std::vector<StructPiece> pieces;
		if (obj->HasOwnVptr()) pieces.push_back({ 0, llvm::PointerType::get(m_Context, 0), sizeof(void*) });
		const auto& bases = obj->GetBaseTypes();
		for (uint32 i = 0; i < bases.Count(); ++i)
		{
			const ObjectType* base = bases[i];
			if (!base || !base->IsValid() || base->GetSize() == 0) continue;
			// A packed-vector base is a SIMD value whose LLVM size can exceed its model size: keep its bytes.
			llvm::Type* ty = base->IsPurePackedVector() ? llvm::ArrayType::get(llvm::Type::getInt8Ty(m_Context), base->GetSize()) : Lower(base);
			pieces.push_back({ obj->GetBaseOffset(i), ty, base->GetSize() });
		}
		// Anonymous-union region members share a byte range: only the FIRST member of each region becomes a
		// piece; the rest alias it.
		int lastRegion = -2;
		for (const auto& f : obj->GetFields())
		{
			if (f.m_IsStatic) continue;
			bool inRegion = f.m_InUnionRegion && f.m_UnionRegionId >= 0;
			bool alias = inRegion && f.m_UnionRegionId == lastRegion && !f.m_UnionRegionFirst;
			lastRegion = inRegion ? f.m_UnionRegionId : -2;
			if (alias) continue;
			pieces.push_back({ f.m_Offset, Storage(f.m_Type), f.m_Type ? f.m_Type->GetSize() : 1 });
		}
		return pieces;
	}

	namespace
	{
		template <typename Unit>
		llvm::Constant* UnitArray(llvm::LLVMContext& context, const std::string& bytes, uint8 width)
		{
			std::vector<Unit> units(bytes.size() / width + 1, 0); // the last unit is the NUL terminator
			for (size_t i = 0; i + 1 < units.size(); ++i)
				for (uint8 b = 0; b < width; ++b)
					units[i] |= static_cast<Unit>(static_cast<unsigned char>(bytes[i * width + b])) << (8 * b);
			return llvm::ConstantDataArray::get(context, llvm::ArrayRef<Unit>(units));
		}
	}

	llvm::GlobalVariable* ModuleContext::StringGlobal(const ConstantValue& value)
	{
		auto key = std::make_pair(value.unitWidth, value.stringValue);
		auto it = m_StringGlobals.find(key);
		if (it != m_StringGlobals.end())
			return it->second;
		llvm::Constant* data = value.unitWidth == 4 ? UnitArray<uint32_t>(m_Context, value.stringValue, 4)
			: value.unitWidth == 2 ? UnitArray<uint16_t>(m_Context, value.stringValue, 2)
			: llvm::ConstantDataArray::getString(m_Context, value.stringValue, true);
		auto* global = new llvm::GlobalVariable(*m_Module, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data,
			StableNames::ForBytes(".str." + std::to_string(value.unitWidth), value.stringValue));
		global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
		m_StringGlobals.emplace(std::move(key), global);
		return global;
	}

	void ModuleContext::StoreZero(llvm::IRBuilder<>& B, const Type* type, llvm::Value* at)
	{
		llvm::Type* lowered = Lower(type);
		if (!lowered->isAggregateType())
		{
			B.CreateStore(llvm::Constant::getNullValue(lowered), at);
			return;
		}
		const uint64_t size = module().getDataLayout().getTypeAllocSize(lowered);
		B.CreateMemSet(at, B.getInt8(0), size, llvm::Align(type && type->GetAlignment() ? type->GetAlignment() : 1));
	}

	llvm::Constant* ModuleContext::ConstantOf(const ConstantValue& value, const Type* type)
	{
		auto& ctx = m_Context;
		llvm::Type* ty = Lower(type);

		// A string literal is the address of its code units (char8* / char16* / char32*).
		if (value.kind == ConstantValue::Kind::String)
			return StringGlobal(value);

		// A __uuidof(T) constant is a pointer to the 16-byte COM GUID: emit (or reuse) a private
		// [16 x i8] global holding the bytes in Win32 _GUID layout and return its address.
		if (value.kind == ConstantValue::Kind::Guid)
		{
			auto& module = *m_Module;
			std::string name = ".guid.";
			for (int i = 0; i < 16; ++i) { std::string d = std::to_string(value.guidBytes[i]); name += d; name += "_"; }
			if (auto* existing = module.getNamedGlobal(name.c_str()))
				return existing;
			llvm::Type* byteTy = llvm::Type::getInt8Ty(ctx);
			llvm::Constant* inits[16];
			for (int i = 0; i < 16; ++i)
				inits[i] = llvm::ConstantInt::get(byteTy, value.guidBytes[i]);
			auto* data = llvm::ConstantArray::get(llvm::ArrayType::get(byteTy, 16), inits);
			auto* gv = new llvm::GlobalVariable(module, data->getType(), true,
				llvm::GlobalValue::PrivateLinkage, data, name);
			return gv;
		}

		// A pointer-typed constant is always the null value (C4 has no raw addresses): `null` lowers to a void*
		// zero and converts to whatever pointer type it's used as. It must be emitted as a null POINTER, not an
		// integer - ConstantInt::get(ptrTy, 0) is invalid IR.
		if (ty->isPointerTy())
			return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ty));

		return NumberConstant(value, type, ty);
	}

	llvm::Constant* ModuleContext::NumberConstant(const ConstantValue& value, const Type* type, llvm::Type* ty)
	{
		// A normalized type stores the value's encoding (D-38); an integer takes the integer value (a float one
		// truncates, as a conversion does), a float the floating value; any other storage starts zero.
		const Type* stored = TypeHandle::UnwrapToValue(type);
		auto integer = [&](int64 v) {
			const uint32 bits = ty->getIntegerBitWidth();
			const uint64 mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
			return llvm::ConstantInt::get(ty, static_cast<uint64>(v) & mask);
		};
		const bool isFloat = value.kind == ConstantValue::Kind::Float64;
		if (ty->isIntegerTy() && stored && stored->IsNormalized()) return integer(NormalizedConversion::RawOf(value, stored));
		if (ty->isIntegerTy()) return integer(isFloat ? static_cast<int64>(value.floatValue) : value.AsInt());
		if (ty->isFloatingPointTy()) return llvm::ConstantFP::get(ty, isFloat ? value.floatValue : static_cast<double>(value.AsInt()));
		return llvm::Constant::getNullValue(ty);
	}

	llvm::Function* ModuleContext::RuntimeFunction(const char* name, llvm::Type* ret, std::vector<llvm::Type*> params)
	{
		auto* fn = m_Module->getFunction(name);
		if (fn) return fn;
		auto* fty = llvm::FunctionType::get(ret, params, false);
		fn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, name, m_Module);
		return fn;
	}

	// --- Exception handling: the frame handler and the raise (the tables are EhTables) ---------------
	llvm::Function* ModuleContext::GetCxxThrowException()
	{
		auto* f = m_Module->getFunction("_CxxThrowException");
		if (!f)
		{
			f = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(m_Context),
				{ llvm::PointerType::get(m_Context, 0), llvm::PointerType::get(m_Context, 0) }, false),
				llvm::Function::ExternalLinkage, "_CxxThrowException", m_Module);
			f->setDoesNotReturn(); // the raise never returns to the throw site
		}
		return f;
	}

	llvm::Function* ModuleContext::GetFrameHandler3()
	{
		auto* f = m_Module->getFunction("__CxxFrameHandler3");
		if (!f)
			f = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getInt32Ty(m_Context), {}, true),
				llvm::Function::ExternalLinkage, "__CxxFrameHandler3", m_Module);
		return f;
	}

	void ModuleContext::EnableDebugInfo(std::unordered_map<uint32, std::string> sourcePaths)
	{
		m_Debug = std::make_unique<DebugInfo>(*m_Module, std::move(sourcePaths));
	}
} // namespace Scripting
