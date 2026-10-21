#include "Script/ScriptDllImports.h"
#include "Script/ScriptSymbols.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Object/COFF.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Target/TargetMachine.h>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Scripting
{
	namespace
	{
		constexpr const char* kImportPrefix = "__imp_";
		constexpr const char* kSlots = "__c4_import_slots";
		constexpr const char* kThunks = "__c4_import_thunks";   // each import's thunk, or null
		constexpr const char* kStandIns = "__c4_import_standins";
		constexpr const char* kImageBase = "__ImageBase";       // the linker's, in every image
		// A data stand-in's room: the script's constants may point this far into a host object (a vtable's address point,
		// a member, an element).
		constexpr uint64 kStandInBytes = 64 * 1024;

		bool Fail(std::string* error, const std::string& why)
		{
			if (error) *error = why;
			return false;
		}

		std::optional<std::unique_ptr<llvm::object::ObjectFile>> Read(const std::string& object, std::string* error)
		{
			auto file = llvm::object::ObjectFile::createObjectFile(llvm::MemoryBufferRef(object, "script"));
			if (file) return std::move(*file);
			Fail(error, "the script's object does not read: " + llvm::toString(file.takeError()));
			return std::nullopt;
		}

		// The global names an object defines or leaves to the link.
		std::vector<std::string> GlobalNames(const llvm::object::ObjectFile& file, bool undefined)
		{
			std::vector<std::string> names;
			for (const llvm::object::SymbolRef& symbol : file.symbols())
			{
				auto flags = symbol.getFlags();
				auto name = symbol.getName();
				if (!flags || !name)
				{
					llvm::consumeError(flags.takeError());
					llvm::consumeError(name.takeError());
					continue;
				}
				const bool global = *flags & llvm::object::SymbolRef::SF_Global;
				const bool isUndefined = *flags & llvm::object::SymbolRef::SF_Undefined;
				if (global && isUndefined == undefined) names.push_back(name->str());
			}
			return names;
		}

		// The names code calls or loads directly, and the ones an image-relative offset names (the unwind tables'
		// personality routine): each must be code in the image.
		std::unordered_set<std::string> NamesCodeReaches(const llvm::object::ObjectFile& file)
		{
			std::unordered_set<std::string> names;
			for (const llvm::object::SectionRef& section : file.sections())
				for (const llvm::object::RelocationRef& relocation : section.relocations())
				{
					if (!section.isText() && relocation.getType() != IMAGE_REL_AMD64_ADDR32NB) continue;
					const llvm::object::symbol_iterator symbol = relocation.getSymbol();
					if (symbol == file.symbol_end()) continue;
					auto name = symbol->getName();
					if (name) names.insert(name->str());
					else llvm::consumeError(name.takeError());
				}
			return names;
		}

		ScriptImport ImportOf(const std::string& symbol, const std::unordered_set<std::string>& codeReaches)
		{
			ScriptImport import;
			if (symbol.rfind(kImportPrefix, 0) == 0)
			{
				import.name = symbol.substr(std::strlen(kImportPrefix));
				return import;
			}
			import.name = symbol;
			import.kind = codeReaches.count(symbol) ? ImportKind::Thunk : ImportKind::Data;
			return import;
		}

		void AddThunk(llvm::Module& module, const std::string& name, const std::string& slot)
		{
			llvm::LLVMContext& context = module.getContext();
			auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(context), false);
			auto* thunk = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage, name, module);
			thunk->addFnAttr(llvm::Attribute::Naked);
			thunk->addFnAttr(llvm::Attribute::NoUnwind);
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(context, "entry", thunk));
			B.CreateCall(type, llvm::InlineAsm::get(type, "jmpq *" + slot + "(%rip)", "", true, false, llvm::InlineAsm::AD_ATT));
			B.CreateUnreachable();
		}

		llvm::GlobalVariable* ExportedArray(llvm::Module& module, llvm::Type* element, uint64 count, const char* name, llvm::Constant* value = nullptr)
		{
			auto* type = llvm::ArrayType::get(element, std::max<uint64>(count, 1));
			auto* array = new llvm::GlobalVariable(module, type, false, llvm::GlobalValue::ExternalLinkage,
				value ? value : llvm::ConstantAggregateZero::get(type), name);
			array->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
			return array;
		}

		llvm::Constant* Element(llvm::GlobalVariable* array, uint64 index)
		{
			auto* int64 = llvm::Type::getInt64Ty(array->getContext());
			llvm::Constant* indexes[] = { llvm::ConstantInt::get(int64, 0), llvm::ConstantInt::get(int64, index) };
			return llvm::ConstantExpr::getInBoundsGetElementPtr(array->getValueType(), array, indexes);
		}

		uint64 DataCount(const std::vector<ScriptImport>& imports)
		{
			uint64 count = 0;
			for (const ScriptImport& import : imports) count += import.kind == ImportKind::Data ? 1 : 0;
			return count;
		}

		// Where a loaded address now points instead: the host's, for a thunk or a stand-in.
		class Rebinding
		{
		public:
			Rebinding(const std::vector<ScriptImport>& imports, void* const* thunks, const uint8* standIns) : m_StandIns(standIns)
			{
				for (size_t i = 0; i < imports.size(); ++i)
				{
					if (imports[i].kind == ImportKind::Thunk) m_Thunks[reinterpret_cast<uint64>(thunks[i])] = reinterpret_cast<uint64>(imports[i].address);
					if (imports[i].kind == ImportKind::Data) m_Data.push_back(reinterpret_cast<uint64>(imports[i].address));
				}
			}

			std::optional<uint64> For(uint64 address) const
			{
				if (auto thunk = m_Thunks.find(address); thunk != m_Thunks.end()) return thunk->second;
				const uint64 start = reinterpret_cast<uint64>(m_StandIns);
				if (address < start || address >= start + m_Data.size() * kStandInBytes) return std::nullopt;
				return m_Data[(address - start) / kStandInBytes] + (address - start) % kStandInBytes;
			}

		private:
			std::unordered_map<uint64, uint64> m_Thunks;
			std::vector<uint64> m_Data;
			const uint8* m_StandIns;
		};

		void Write(uint64* at, uint64 value)
		{
			DWORD protection = 0;
			VirtualProtect(at, sizeof(*at), PAGE_READWRITE, &protection);
			*at = value;
			VirtualProtect(at, sizeof(*at), protection, &protection);
		}

		// Every 64-bit address the loader relocated, outside the thunk table, that names a thunk or a stand-in.
		void Rebind(uint8* base, const Rebinding& rebinding, const void* thunkTable, uint64 thunkTableBytes)
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
			const uint8* at = base + directory.VirtualAddress;
			const uint8* end = at + directory.Size;
			const auto* tableStart = static_cast<const uint8*>(thunkTable);
			while (at + sizeof(IMAGE_BASE_RELOCATION) <= end)
			{
				const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(at);
				if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) break;
				const auto* entries = reinterpret_cast<const WORD*>(block + 1);
				const size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
				for (size_t i = 0; i < count; ++i)
				{
					if ((entries[i] >> 12) != IMAGE_REL_BASED_DIR64) continue;
					auto* location = reinterpret_cast<uint64*>(base + block->VirtualAddress + (entries[i] & 0xfff));
					const auto* byte = reinterpret_cast<const uint8*>(location);
					if (byte >= tableStart && byte < tableStart + thunkTableBytes) continue;
					if (std::optional<uint64> host = rebinding.For(*location)) Write(location, *host);
				}
				at += block->SizeOfBlock;
			}
		}
	}

	std::optional<std::vector<std::string>> ScriptDefinitions(const std::string& object, std::string* error)
	{
		auto file = Read(object, error);
		if (!file) return std::nullopt;
		return GlobalNames(**file, false);
	}

	std::optional<std::vector<ScriptImport>> ScriptImports(const std::string& object, const HostSymbolTable& host, std::string* error)
	{
		auto file = Read(object, error);
		if (!file) return std::nullopt;
		const std::unordered_set<std::string> codeReaches = NamesCodeReaches(**file);
		std::vector<ScriptImport> imports;
		std::string missing;
		for (const std::string& symbol : GlobalNames(**file, true))
		{
			if (symbol == kImageBase || symbol == kFloatingPointMarker) continue;
			ScriptImport import = ImportOf(symbol, codeReaches);
			import.address = ResolveScriptSymbol(host, import.name);
			if (!import.address) missing += (missing.empty() ? "" : ", ") + import.name;
			imports.push_back(std::move(import));
		}
		if (missing.empty()) return imports;
		Fail(error, "the script does not link: nothing defines " + missing);
		return std::nullopt;
	}

	std::unique_ptr<llvm::Module> ScriptImportModule(llvm::LLVMContext& context, const std::vector<ScriptImport>& imports,
		const llvm::TargetMachine& machine)
	{
		auto module = std::make_unique<llvm::Module>("c4.imports", context);
		module->setDataLayout(machine.createDataLayout());
		module->setTargetTriple(machine.getTargetTriple());
		auto* pointer = llvm::PointerType::get(context, 0);
		llvm::GlobalVariable* slots = ExportedArray(*module, pointer, imports.size(), kSlots);
		llvm::GlobalVariable* standIns = ExportedArray(*module, llvm::Type::getInt8Ty(context), DataCount(imports) * kStandInBytes, kStandIns);
		new llvm::GlobalVariable(*module, llvm::Type::getInt32Ty(context), true, llvm::GlobalValue::ExternalLinkage,
			llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0x9875), kFloatingPointMarker);
		std::vector<llvm::Constant*> thunks;
		uint64 data = 0;
		for (size_t i = 0; i < imports.size(); ++i)
		{
			const ScriptImport& import = imports[i];
			llvm::Function* thunk = nullptr;
			if (import.kind == ImportKind::Slot)
				llvm::GlobalAlias::create(pointer, 0, llvm::GlobalValue::ExternalLinkage, kImportPrefix + import.name, Element(slots, i), module.get());
			else if (import.kind == ImportKind::Data)
				llvm::GlobalAlias::create(llvm::Type::getInt8Ty(context), 0, llvm::GlobalValue::ExternalLinkage, import.name, Element(standIns, data++ * kStandInBytes), module.get());
			else
			{
				const std::string slot = "__c4_slot_" + std::to_string(i);
				llvm::GlobalAlias::create(pointer, 0, llvm::GlobalValue::InternalLinkage, slot, Element(slots, i), module.get());
				AddThunk(*module, import.name, slot);
				thunk = module->getFunction(import.name);
			}
			thunks.push_back(thunk ? static_cast<llvm::Constant*>(thunk) : llvm::ConstantPointerNull::get(pointer));
		}
		if (thunks.empty()) thunks.push_back(llvm::ConstantPointerNull::get(pointer));
		ExportedArray(*module, pointer, thunks.size(), kThunks, llvm::ConstantArray::get(llvm::ArrayType::get(pointer, thunks.size()), thunks));
		return module;
	}

	bool BindScriptImports(void* module, const std::vector<ScriptImport>& imports, std::string* error)
	{
		auto handle = static_cast<HMODULE>(module);
		auto* slots = reinterpret_cast<void**>(GetProcAddress(handle, kSlots));
		auto* thunks = reinterpret_cast<void* const*>(GetProcAddress(handle, kThunks));
		auto* standIns = reinterpret_cast<const uint8*>(GetProcAddress(handle, kStandIns));
		if (!slots || !thunks || !standIns) return Fail(error, "the script's module has no import tables");
		for (size_t i = 0; i < imports.size(); ++i) slots[i] = imports[i].address;
		Rebind(reinterpret_cast<uint8*>(handle), Rebinding(imports, thunks, standIns), thunks, std::max<size_t>(imports.size(), 1) * sizeof(void*));
		return true;
	}
} // namespace Scripting
