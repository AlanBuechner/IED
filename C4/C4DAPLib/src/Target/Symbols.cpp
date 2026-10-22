#include "Target/Symbols.h"
#include "Target/WideText.h"
#include <oaidl.h> // VARIANT, which an enumerator's value comes in
#include <dbghelp.h>
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>

namespace C4DAP::Target
{
	namespace
	{
		// The DIA SymTagEnum, BasicType, DataKind and UdtKind values DbgHelp reports (cvconst.h is not in the SDK).
		enum SymTag : DWORD
		{
			kTagFunction = 5, kTagData = 7, kTagPublicSymbol = 10, kTagUDT = 11, kTagEnum = 12, kTagFunctionType = 13,
			kTagPointerType = 14, kTagArrayType = 15, kTagBaseType = 16, kTagTypedef = 17, kTagBaseClass = 18,
		};
		enum BasicType : DWORD
		{
			kBasicVoid = 1, kBasicChar = 2, kBasicWChar = 3, kBasicInt = 6, kBasicUInt = 7, kBasicFloat = 8, kBasicBool = 10,
			kBasicLong = 13, kBasicULong = 14, kBasicChar16 = 32, kBasicChar32 = 33, kBasicChar8 = 34,
		};
		constexpr DWORD kDataIsMember = 7;
		constexpr DWORD kUdtUnion = 2;
		// Lines a compiler marks as belonging to no statement.
		constexpr uint32_t kFirstHiddenLine = 0xF00000;
		// A breakpoint on a line without code binds to the next line that has some, at most this far below.
		constexpr uint32_t kNextLineSearch = 200;
		constexpr ULONG kMaxNameChars = 512;
		// DbgHelp's SYMFLAG_NULL: the symbol has no location at the scope's address (older SDKs lack the name).
		constexpr ULONG kNoLocationHere = 0x00080000;
		// A line's code: out of line, then where calls of its function were inlined (each search finds only its own).
		constexpr DWORD kLineSearches[] = { ESLFLAG_FULLPATH, ESLFLAG_FULLPATH | ESLFLAG_INLINE_SITE };
		constexpr ULONG kMaxChildren = 4096;

		struct SymbolBuffer
		{
			SymbolBuffer()
			{
				info.SizeOfStruct = sizeof(SYMBOL_INFOW);
				info.MaxNameLen = kMaxNameChars;
			}
			SYMBOL_INFOW info{};
			wchar_t nameTail[kMaxNameChars]{};
		};

		BasicKind KindOf(DWORD basic)
		{
			switch (basic)
			{
			case kBasicInt: case kBasicLong: return BasicKind::Signed;
			case kBasicUInt: case kBasicULong: return BasicKind::Unsigned;
			case kBasicFloat: return BasicKind::Float;
			case kBasicBool: return BasicKind::Bool;
			case kBasicChar: case kBasicChar8: return BasicKind::Char8;
			case kBasicWChar: case kBasicChar16: return BasicKind::Char16;
			case kBasicChar32: return BasicKind::Char32;
			default: return BasicKind::Other;
			}
		}

		SymbolStorage StorageOfSymbol(const SYMBOL_INFOW& symbol)
		{
			return StorageOf(symbol.Flags, symbol.Register, symbol.Address, symbol.Value);
		}

		std::wstring PlatformPath(const std::string& path)
		{
			std::wstring wide = Wide(path);
			std::replace(wide.begin(), wide.end(), L'/', L'\\');
			return wide;
		}

		bool Hidden(uint32_t line) { return line == 0 || line >= kFirstHiddenLine; }

		// Windows paths are case-insensitive and take either slash.
		bool SamePath(std::wstring a, std::wstring b)
		{
			for (std::wstring* path : { &a, &b })
				for (wchar_t& c : *path) c = c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c));
			return a == b;
		}
	}

	SymbolStorage StorageOf(uint32_t flags, uint32_t reg, uint64_t address, uint64_t value)
	{
		SymbolStorage storage;
		storage.reg = reg;
		storage.offset = static_cast<int64_t>(address);
		storage.constant = value;
		if (flags & kNoLocationHere) storage.kind = SymbolStorage::Kind::OptimizedAway;
		else if (flags & (SYMFLAG_CONSTANT | SYMFLAG_VALUEPRESENT)) storage.kind = SymbolStorage::Kind::Constant;
		else if (flags & SYMFLAG_REGREL) storage.kind = SymbolStorage::Kind::RegisterRelative;
		else if (flags & SYMFLAG_FRAMEREL) storage.kind = SymbolStorage::Kind::FrameRelative;
		else if (flags & SYMFLAG_REGISTER) storage.kind = SymbolStorage::Kind::Register;
		return storage;
	}

	bool Symbols::Open(HANDLE process)
	{
		// Not deferred: a source-line search only looks in modules whose symbols are already loaded.
		SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
		if (!SymInitializeW(process, nullptr, FALSE)) return false;
		m_Process = process;
		return true;
	}

	void Symbols::Close()
	{
		if (m_Process) SymCleanup(m_Process);
		m_Process = nullptr;
		m_Modules.clear();
	}

	void Symbols::ModuleLoaded(HANDLE file, uint64_t base)
	{
		if (m_Process && SymLoadModuleExW(m_Process, file, nullptr, nullptr, base, 0, nullptr, 0)) m_Modules.push_back(base);
	}

	void Symbols::ModuleUnloaded(uint64_t base)
	{
		if (m_Process) SymUnloadModule64(m_Process, base);
		std::erase(m_Modules, base);
	}

	std::vector<uint64_t> Symbols::ExactLine(const std::wstring& path, uint32_t line) const
	{
		struct Found { uint32_t line; std::vector<uint64_t> addresses; } found{ line, {} };
		const auto collect = [](PSRCCODEINFOW info, PVOID context) -> BOOL {
			auto* found = static_cast<Found*>(context);
			if (info->LineNumber == found->line) found->addresses.push_back(info->Address);
			return TRUE;
		};
		for (uint64_t module : m_Modules)
			for (DWORD search : kLineSearches) SymEnumSourceLinesW(m_Process, module, nullptr, path.c_str(), line, search, collect, &found);
		std::sort(found.addresses.begin(), found.addresses.end());
		found.addresses.erase(std::unique(found.addresses.begin(), found.addresses.end()), found.addresses.end());
		return OwnCode(found.addresses, path, line);
	}

	// A function's line table gives the code of a call it inlined the call's line, all the way through, so a
	// breakpoint on each stretch of it would stop over and over (each time round a loop in the inlined function). A
	// line binds to its own code; a function whose code for the line is all inlined calls, to the first of it.
	std::vector<uint64_t> Symbols::OwnCode(const std::vector<uint64_t>& addresses, const std::wstring& path, uint32_t line) const
	{
		std::vector<uint64_t> own;
		std::map<uint64_t, uint64_t> firstInlined; // a function's start to its first address on the line that is an inlined call's
		for (uint64_t address : addresses) // in order: the first stays
		{
			if (OnLineItself(address, path, line)) own.push_back(address);
			else firstInlined.emplace(FunctionStart(address), address);
		}
		std::set<uint64_t> withOwnCode;
		for (uint64_t address : own) withOwnCode.insert(FunctionStart(address));
		for (const auto& [function, address] : firstInlined)
			if (!withOwnCode.count(function)) own.push_back(address);
		std::sort(own.begin(), own.end());
		return own;
	}

	// Code outside any inlined call is its function's own, so on the line its line table says; inside one, the
	// innermost inlined call's line is the code's.
	bool Symbols::OnLineItself(uint64_t address, const std::wstring& path, uint32_t line) const
	{
		const InlinedCalls inlined = InlinedAt(address);
		if (inlined.depth == 0) return true;
		const SourcePosition innermost = PositionOf(CodePlace{ address, inlined.innermost });
		return innermost.line == line && SamePath(Wide(innermost.path), path);
	}

	uint64_t Symbols::FunctionStart(uint64_t address) const
	{
		const std::optional<FunctionFacts> function = FunctionOf(CodePlace{ address });
		return function ? function->start : 0;
	}

	std::vector<uint64_t> Symbols::AddressesOfLine(const std::string& path, uint32_t line, uint32_t& bound) const
	{
		bound = 0;
		if (!m_Process || line == 0) return {};
		const std::wstring file = PlatformPath(path);
		for (uint32_t candidate = line; candidate < line + kNextLineSearch; ++candidate)
		{
			std::vector<uint64_t> addresses = ExactLine(file, candidate);
			if (!addresses.empty())
			{
				bound = candidate;
				return addresses;
			}
		}
		return {};
	}

	SourcePosition Symbols::PositionOf(CodePlace place) const
	{
		IMAGEHLP_LINEW64 line{ sizeof(IMAGEHLP_LINEW64) };
		DWORD displacement = 0;
		if (!m_Process) return {};
		const BOOL found = place.inlineContext ? SymGetLineFromInlineContextW(m_Process, place.address, place.inlineContext, 0, &displacement, &line)
			: SymGetLineFromAddrW64(m_Process, place.address, &displacement, &line);
		if (!found || Hidden(line.LineNumber)) return {};
		return SourcePosition{ Narrow(line.FileName), line.LineNumber };
	}

	uint64_t Symbols::NextStatementFrom(uint64_t address) const
	{
		IMAGEHLP_LINEW64 line{ sizeof(IMAGEHLP_LINEW64) };
		DWORD displacement = 0;
		if (!m_Process || !SymGetLineFromAddrW64(m_Process, address, &displacement, &line) || displacement == 0) return address;
		while (SymGetLineNextW64(m_Process, &line))
			if (line.Address > address && !Hidden(line.LineNumber)) return line.Address;
		return address;
	}

	std::optional<FunctionFacts> Symbols::FunctionOf(CodePlace place) const
	{
		SymbolBuffer symbol;
		DWORD64 displacement = 0;
		if (!m_Process) return std::nullopt;
		const BOOL found = place.inlineContext ? SymFromInlineContextW(m_Process, place.address, place.inlineContext, &displacement, &symbol.info)
			: SymFromAddrW(m_Process, place.address, &displacement, &symbol.info);
		if (!found) return std::nullopt;
		return FunctionFacts{ Narrow(symbol.info.Name), symbol.info.Address, symbol.info.Size };
	}

	InlinedCalls Symbols::InlinedAt(uint64_t address) const
	{
		if (!m_Process) return {};
		const DWORD depth = SymAddrIncludeInlineTrace(m_Process, address);
		DWORD innermost = 0, index = 0;
		if (depth == 0 || !SymQueryInlineTrace(m_Process, address, INLINE_FRAME_CONTEXT_INIT, address, address, &innermost, &index)) return {};
		return InlinedCalls{ static_cast<uint32_t>(depth), static_cast<uint32_t>(innermost) };
	}

	std::vector<uint64_t> Symbols::FunctionsNamed(const std::string& name) const
	{
		struct Found { const Symbols* self; std::vector<uint64_t> entries; } found{ this, {} };
		const std::wstring mask = L"*!" + Wide(name);
		SymEnumSymbolsW(m_Process, 0, mask.c_str(),
			[](PSYMBOL_INFOW symbol, ULONG, PVOID context) -> BOOL {
				auto* found = static_cast<Found*>(context);
				if (symbol->Tag == kTagFunction && found->self->PositionOf(CodePlace{ symbol->Address }).Known())
					found->entries.push_back(symbol->Address);
				return TRUE;
			}, &found);
		return found.entries;
	}

	std::optional<uint64_t> Symbols::PublicFunction(const std::string& name) const
	{
		SymbolBuffer symbol;
		if (!m_Process || !SymFromNameW(m_Process, Wide(name).c_str(), &symbol.info)) return std::nullopt;
		return symbol.info.Address;
	}

	void Symbols::SetScope(CodePlace place) const
	{
		if (place.inlineContext)
		{
			SymSetScopeFromInlineContext(m_Process, place.address, place.inlineContext);
			return;
		}
		IMAGEHLP_STACK_FRAME frame{};
		frame.InstructionOffset = place.address;
		SymSetContext(m_Process, &frame, nullptr); // FALSE with no error when the scope is already this one
	}

	std::vector<VariableSymbol> Symbols::VariablesAt(CodePlace place) const
	{
		std::vector<VariableSymbol> variables;
		if (!m_Process) return variables;
		SetScope(place);
		SymEnumSymbolsW(m_Process, 0, nullptr,
			[](PSYMBOL_INFOW symbol, ULONG, PVOID context) -> BOOL {
				if (!(symbol->Flags & (SYMFLAG_LOCAL | SYMFLAG_PARAMETER))) return TRUE;
				auto* variables = static_cast<std::vector<VariableSymbol>*>(context);
				variables->push_back(VariableSymbol{ Narrow(symbol->Name), TypeRef{ symbol->ModBase, symbol->TypeIndex }, StorageOfSymbol(*symbol),
					(symbol->Flags & SYMFLAG_PARAMETER) != 0 });
				return TRUE;
			}, &variables);
		std::stable_partition(variables.begin(), variables.end(), [](const VariableSymbol& v) { return v.parameter; });
		return variables;
	}

	std::optional<VariableSymbol> Symbols::Global(const std::string& name) const
	{
		SymbolBuffer symbol;
		if (!m_Process || !SymFromNameW(m_Process, Wide(name).c_str(), &symbol.info) || symbol.info.Tag != kTagData) return std::nullopt;
		return VariableSymbol{ Narrow(symbol.info.Name), TypeRef{ symbol.info.ModBase, symbol.info.TypeIndex }, StorageOfSymbol(symbol.info), false };
	}

	uint32_t Symbols::Tag(TypeRef type) const
	{
		DWORD tag = 0;
		SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_SYMTAG, &tag);
		return tag;
	}

	TypeRef Symbols::WithoutAliases(TypeRef type) const
	{
		for (int depth = 0; depth < 16 && Tag(type) == kTagTypedef; ++depth)
		{
			DWORD aliased = 0;
			if (!SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_TYPEID, &aliased)) break;
			type.id = aliased;
		}
		return type;
	}

	TypeFacts Symbols::Describe(TypeRef type) const
	{
		TypeFacts facts;
		if (!m_Process || !type.Valid()) return facts;
		std::string alias;
		if (Tag(type) == kTagTypedef)
		{
			wchar_t* name = nullptr;
			if (SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_SYMNAME, &name) && name) { alias = Narrow(name); LocalFree(name); }
			type = WithoutAliases(type);
		}
		ULONG64 length = 0;
		SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_LENGTH, &length);
		facts.size = length;
		DWORD element = 0;
		const bool hasElement = SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_TYPEID, &element) != FALSE;
		if (hasElement) facts.element = TypeRef{ type.module, element };
		switch (Tag(type))
		{
		case kTagBaseType:
		{
			DWORD basic = 0;
			SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_BASETYPE, &basic);
			facts.shape = basic == kBasicVoid ? TypeShape::Void : TypeShape::Basic;
			facts.basic = KindOf(basic);
			break;
		}
		case kTagPointerType:
		{
			BOOL reference = FALSE;
			SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_IS_REFERENCE, &reference);
			facts.shape = reference ? TypeShape::Reference : TypeShape::Pointer;
			break;
		}
		case kTagUDT:
		{
			DWORD kind = 0;
			SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_UDTKIND, &kind);
			facts.shape = kind == kUdtUnion ? TypeShape::Union : TypeShape::Struct;
			break;
		}
		case kTagEnum: facts.shape = TypeShape::Enum; break;
		case kTagArrayType:
		{
			DWORD count = 0;
			SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_COUNT, &count);
			facts.shape = TypeShape::Array;
			facts.count = count;
			break;
		}
		case kTagFunctionType: facts.shape = TypeShape::Function; break;
		default: break;
		}
		if (facts.shape == TypeShape::Struct || facts.shape == TypeShape::Union || facts.shape == TypeShape::Enum)
		{
			wchar_t* name = nullptr;
			if (SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_SYMNAME, &name) && name) { facts.name = Narrow(name); LocalFree(name); }
		}
		if (!alias.empty()) facts.name = alias;
		return facts;
	}

	std::vector<uint32_t> Symbols::Children(TypeRef type) const
	{
		DWORD count = 0;
		if (!SymGetTypeInfo(m_Process, type.module, type.id, TI_GET_CHILDRENCOUNT, &count) || count == 0) return {};
		count = std::min<DWORD>(count, kMaxChildren);
		std::vector<uint8_t> storage(sizeof(TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG));
		auto* params = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(storage.data());
		params->Count = count;
		params->Start = 0;
		if (!SymGetTypeInfo(m_Process, type.module, type.id, TI_FINDCHILDREN, params)) return {};
		return std::vector<uint32_t>(params->ChildId, params->ChildId + count);
	}

	std::vector<MemberFacts> Symbols::Members(TypeRef type) const
	{
		std::vector<MemberFacts> members;
		if (!m_Process || !type.Valid()) return members;
		type = WithoutAliases(type);
		for (uint32_t child : Children(type))
		{
			const TypeRef member{ type.module, child };
			const uint32_t tag = Tag(member);
			DWORD kind = 0;
			if (tag == kTagData && (!SymGetTypeInfo(m_Process, member.module, member.id, TI_GET_DATAKIND, &kind) || kind != kDataIsMember)) continue;
			if (tag != kTagData && tag != kTagBaseClass) continue;
			MemberFacts facts;
			facts.isBase = tag == kTagBaseClass;
			DWORD memberType = 0, offset = 0;
			SymGetTypeInfo(m_Process, member.module, member.id, TI_GET_TYPEID, &memberType);
			SymGetTypeInfo(m_Process, member.module, member.id, TI_GET_OFFSET, &offset);
			facts.type = TypeRef{ type.module, memberType };
			facts.offset = offset;
			wchar_t* name = nullptr;
			if (SymGetTypeInfo(m_Process, member.module, member.id, TI_GET_SYMNAME, &name) && name) { facts.name = Narrow(name); LocalFree(name); }
			if (facts.isBase && facts.name.empty()) facts.name = Describe(facts.type).name;
			members.push_back(std::move(facts));
		}
		std::stable_partition(members.begin(), members.end(), [](const MemberFacts& m) { return m.isBase; });
		return members;
	}

	std::vector<Enumerator> Symbols::Enumerators(TypeRef type) const
	{
		std::vector<Enumerator> values;
		if (!m_Process || !type.Valid()) return values;
		type = WithoutAliases(type);
		for (uint32_t child : Children(type))
		{
			VARIANT value{};
			wchar_t* name = nullptr;
			if (!SymGetTypeInfo(m_Process, type.module, child, TI_GET_VALUE, &value)) continue;
			if (!SymGetTypeInfo(m_Process, type.module, child, TI_GET_SYMNAME, &name) || !name) continue;
			int64_t number = 0;
			switch (value.vt)
			{
			case VT_I1: number = value.cVal; break;
			case VT_I2: number = value.iVal; break;
			case VT_I4: case VT_INT: number = value.lVal; break;
			case VT_I8: number = value.llVal; break;
			case VT_UI1: number = value.bVal; break;
			case VT_UI2: number = value.uiVal; break;
			case VT_UI4: case VT_UINT: number = value.ulVal; break;
			case VT_UI8: number = static_cast<int64_t>(value.ullVal); break;
			default: break;
			}
			values.push_back(Enumerator{ Narrow(name), number });
			LocalFree(name);
		}
		return values;
	}
} // namespace C4DAP::Target
